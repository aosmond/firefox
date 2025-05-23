/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  ANALYSIS_RESPONSE_SCHEMA,
  ANALYSIS_REQUEST_SCHEMA,
  ANALYZE_RESPONSE_SCHEMA,
  ANALYZE_REQUEST_SCHEMA,
  ANALYSIS_STATUS_RESPONSE_SCHEMA,
  ANALYSIS_STATUS_REQUEST_SCHEMA,
  RECOMMENDATIONS_RESPONSE_SCHEMA,
  RECOMMENDATIONS_REQUEST_SCHEMA,
  ATTRIBUTION_RESPONSE_SCHEMA,
  ATTRIBUTION_REQUEST_SCHEMA,
  REPORTING_RESPONSE_SCHEMA,
  REPORTING_REQUEST_SCHEMA,
  ProductConfig,
  ProductConfigDEFR,
  ShoppingEnvironment,
} from "chrome://global/content/shopping/ProductConfig.mjs";

let { EventEmitter } = ChromeUtils.importESModule(
  "resource://gre/modules/EventEmitter.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ObliviousHTTP: "resource://gre/modules/ObliviousHTTP.sys.mjs",
  ProductValidator: "chrome://global/content/shopping/ProductValidator.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const API_RETRIES = 3;
const API_RETRY_TIMEOUT = 100;
const API_POLL_ATTEMPTS = 260;
const API_POLL_INITIAL_WAIT = 1000;
const API_POLL_WAIT = 1000;

/**
 * @typedef {object} Product
 *  A parsed product for a URL
 * @property {number} id
 *  The product id of the product.
 * @property {string} host
 *  The host of a product url (without www)
 * @property {string} tld
 *  The top level domain of a URL
 * @property {string} sitename
 *  The name of a website (without TLD or subdomains)
 * @property {boolean} valid
 *  If the product is valid or not
 */

/**
 * Status returned from an analysis.
 *
 * @enum {string}
 */
const AnalysisStatus = {
  NOT_FOUND: "not_found",
  COMPLETED: "completed",
  PENDING: "pending",
  IN_PROGRESS: "in_progress",
  NOT_ANALYZABLE: "not_analyzable",
  UNPROCESSABLE: "unprocessable",
  PAGE_NOT_SUPPORTED: "page_not_supported",
  NOT_ENOUGH_REVIEWS: "not_enough_reviews",
  STALE: "stale",
};

/**
 * Class for working with the products shopping API,
 * with helpers for parsing the product from a url
 * and querying the shopping API for information on it.
 *
 * @example
 * let product = new ShoppingProduct(url);
 * if (product.isProduct()) {
 *  let analysis = await product.requestAnalysis();
 *  let recommendations = await product.requestRecommendations();
 * }
 * @example
 * if (!isProductURL(url)) {
 *  return;
 * }
 * let product = new ShoppingProduct(url);
 * let analysis = await product.requestAnalysis();
 * let recommendations = await product.requestRecommendations();
 */
export class ShoppingProduct extends EventEmitter {
  /**
   * Creates a product.
   *
   * @param {URL} url
   *  URL to get the product info from.
   * @param {object} [options]
   * @param {boolean} [options.allowValidationFailure=true]
   *  Should validation failures be allowed or return null
   */
  constructor(url, options = { allowValidationFailure: true }) {
    super();

    this.allowValidationFailure = !!options.allowValidationFailure;

    this._abortController = new AbortController();

    if (url instanceof Ci.nsIURI) {
      url = URL.fromURI(url);
    }

    if (url && URL.isInstance(url)) {
      let product = this.constructor.fromURL(url);
      this.assignProduct(product);
    }
  }

  /**
   * Gets an object of website names and urls supported by Review Checker.
   * This function uses the ProductConfig for validation.
   * The object made is a simplified version of ProductConfig and is meant to be used
   * for content updates.
   *
   * @param {object} [productConfig=ProductConfig]
   *  The ProductConfig to use to determine which sites we can use for reviews.
   * @returns {object | null}
   *  An object mapping website names to arrays of valid url strings, or null if an error occurs.
   */
  static getSupportedDomains(productConfig = ProductConfig) {
    let supportedSites = {};
    try {
      if (
        Services.prefs.getBoolPref(
          "toolkit.shopping.experience2023.defr",
          false
        ) &&
        productConfig === ProductConfig
      ) {
        productConfig = ProductConfigDEFR;
      }
      Object.keys(productConfig).forEach(sitename => {
        let tldsMap = productConfig[sitename].validTLDs.map(tld => {
          return `https://${sitename}.${tld}`;
        });
        supportedSites[sitename] = tldsMap;
      });
      return supportedSites;
    } catch {
      console.error("Failed to get supported sites from config.");
      return null;
    }
  }

  /**
   * Gets a Product from a URL.
   *
   * @param {URL} url
   *  URL to find a product in.
   * @returns {Product}
   */
  static fromURL(url) {
    let host, sitename, tld;
    let result = { valid: false };

    if (!url || !URL.isInstance(url)) {
      return result;
    }

    // Lowercase hostname and remove www.
    host = url.hostname.replace(/^www\./i, "");
    result.host = host;

    // Get host TLD
    try {
      tld = Services.eTLD.getPublicSuffixFromHost(host);
    } catch (_) {
      return result;
    }
    if (!tld.length) {
      return result;
    }

    // Remove tld and the preceding period to get the sitename
    sitename = host.slice(0, -(tld.length + 1));

    // Check if sitename is one the API has products for
    let siteConfig = ProductConfig[sitename];

    if (
      Services.prefs.getBoolPref("toolkit.shopping.experience2023.defr", false)
    ) {
      siteConfig = ProductConfigDEFR[sitename];
    }

    if (!siteConfig) {
      return result;
    }
    result.sitename = sitename;

    // Check if API has products for this TLD
    if (!siteConfig.validTLDs.includes(tld)) {
      return result;
    }
    result.tld = tld;

    // Try to find a product id from the pathname.
    let found = url.pathname.match(siteConfig.productIdFromURLRegex);
    if (!found?.groups) {
      return result;
    }

    let { productId } = found.groups;
    if (!productId) {
      return result;
    }
    result.id = productId;

    // Mark product as valid and complete.
    result.valid = true;

    return result;
  }

  /**
   * Check if a Product is a valid product.
   *
   * @param {Product} product
   *  Product to check.
   * @returns {boolean}
   */
  static isProduct(product) {
    return !!(
      product &&
      product.valid &&
      product.id &&
      product.host &&
      product.sitename &&
      product.tld
    );
  }

  /**
   * Check if an invalid Product is a supported site.
   *
   * @param {Product} product
   *  Product to check.
   * @returns {boolean}
   */
  static isSupportedSite(product) {
    return !!(
      product &&
      !product.valid &&
      product.host &&
      product.sitename &&
      product.tld
    );
  }

  /**
   * Check if an analysis is still in progress.
   *
   * @param {AnalysisStatus} analysisStatus
   * @returns {boolean}
   */
  static isAnalysisInProgress(analysisStatus) {
    if (!analysisStatus) {
      return false;
    }
    return (
      analysisStatus == AnalysisStatus.PENDING ||
      analysisStatus == AnalysisStatus.IN_PROGRESS
    );
  }

  /**
   * Check if an analysis has finished.
   *
   * @param {AnalysisStatus} analysisStatus
   * @returns {boolean}
   */
  static hasAnalysisFinished(analysisStatus) {
    if (!analysisStatus) {
      return true;
    }
    return !ShoppingProduct.isAnalysisInProgress(analysisStatus);
  }

  /**
   * Check if an analysis has completed successfully.
   * Status will be "not_found" if the current analysis is up-to-date.
   *
   * @param {AnalysisStatus} analysisStatus
   * @returns {boolean}
   */
  static hasAnalysisCompleted(analysisStatus) {
    if (!analysisStatus) {
      return false;
    }
    return (
      analysisStatus == AnalysisStatus.COMPLETED ||
      analysisStatus == AnalysisStatus.NOT_FOUND
    );
  }

  /**
   * Check if an analysis has failed.
   *
   * @param {AnalysisStatus} analysisStatus
   * @returns {boolean}
   */
  static hasAnalysisFailed(analysisStatus) {
    if (!analysisStatus) {
      return true;
    }
    return (
      !ShoppingProduct.isAnalysisInProgress(analysisStatus) &&
      !ShoppingProduct.hasAnalysisCompleted(analysisStatus)
    );
  }

  /**
   * Check if a the instances product is a valid product.
   *
   * @returns {boolean}
   */
  isProduct() {
    return this.constructor.isProduct(this.product);
  }

  /**
   * Assign a product to this instance.
   */
  assignProduct(product) {
    if (this.constructor.isProduct(product)) {
      this.product = product;
    }
  }

  /**
   * Request analysis for a product from the API.
   *
   * @param {Product} product
   *  Product to request for (defaults to the instances product).
   * @param {object} options
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async requestAnalysis(
    product = this.product,
    options = {
      url: ShoppingEnvironment.ANALYSIS_API,
      requestSchema: ANALYSIS_REQUEST_SCHEMA,
      responseSchema: ANALYSIS_RESPONSE_SCHEMA,
    }
  ) {
    if (!product) {
      return null;
    }

    let requestOptions = {
      product_id: product.id,
      website: product.host,
    };

    let { url, requestSchema, responseSchema } = options;
    let { allowValidationFailure } = this;

    let result = await ShoppingProduct.request(url, requestOptions, {
      requestSchema,
      responseSchema,
      allowValidationFailure,
    });

    return result;
  }

  /**
   * Request recommended related products from the API.
   * Currently only provides recommendations for Amazon products,
   * which may be paid ads.
   *
   * @param {Product} product
   *  Product to request for (defaults to the instances product).
   * @param {object} options
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async requestRecommendations(
    product = this.product,
    options = {
      url: ShoppingEnvironment.RECOMMENDATIONS_API,
      requestSchema: RECOMMENDATIONS_REQUEST_SCHEMA,
      responseSchema: RECOMMENDATIONS_RESPONSE_SCHEMA,
    }
  ) {
    if (!product) {
      return null;
    }

    let requestOptions = {
      product_id: product.id,
      website: product.host,
    };
    let { url, requestSchema, responseSchema } = options;
    let { allowValidationFailure } = this;
    let result = await ShoppingProduct.request(url, requestOptions, {
      requestSchema,
      responseSchema,
      allowValidationFailure,
    });

    for (let ad of result) {
      ad.image_blob = await ShoppingProduct.requestImageBlob(ad.image_url, {
        signal: this._abortController.signal,
      });
    }

    return result;
  }

  /**
   * Request method for shopping API.
   *
   * @param {string} apiURL
   *  URL string for the API to request.
   * @param {object} bodyObj
   *  What to send to the API.
   * @param {object} [options]
   *  Options for validation and retries.
   * @param {string} [options.requestSchema]
   *  URL string for the JSON Schema to validated the request.
   * @param {string} [options.responseSchema]
   *  URL string for the JSON Schema to validated the response.
   * @param {int} [options.failCount]
   *  Current number of failures for this request.
   * @param {int} [options.maxRetries=API_RETRIES]
   *  Max number of allowed failures.
   * @param {int} [options.retryTimeout=API_RETRY_TIMEOUT]
   *  Minimum time to wait.
   * @param {AbortSignal} [options.signal]
   *  Signal to check if the request needs to be aborted.
   * @param {boolean} [options.allowValidationFailure=true]
   *  Should validation failures be allowed.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  static async request(apiURL, bodyObj = {}, options = {}) {
    let {
      requestSchema,
      responseSchema,
      failCount = 0,
      maxRetries = API_RETRIES,
      retryTimeout = API_RETRY_TIMEOUT,
      signal = new AbortController().signal,
      allowValidationFailure = true,
    } = options;

    if (signal.aborted) {
      return null;
    }

    if (bodyObj && requestSchema) {
      let validRequest = await lazy.ProductValidator.validate(
        bodyObj,
        requestSchema,
        allowValidationFailure
      );
      if (!validRequest) {
        Glean?.shoppingProduct?.invalidRequest.record();
        if (!allowValidationFailure) {
          return null;
        }
      }
    }

    let requestOptions = {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Accept: "application/json",
      },
      body: JSON.stringify(bodyObj),
      signal,
      abortCallback() {
        Glean?.shoppingProduct?.requestAborted.record();
      },
    };

    let requestPromise;
    let ohttpRelayURL = Services.prefs.getStringPref(
      "toolkit.shopping.ohttpRelayURL",
      ""
    );
    let ohttpConfigURL = Services.prefs.getStringPref(
      "toolkit.shopping.ohttpConfigURL",
      ""
    );
    if (ohttpRelayURL && ohttpConfigURL) {
      let config = await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);
      // In the time it took to fetch the OHTTP config, we might have been
      // aborted...
      if (signal.aborted) {
        Glean?.shoppingProduct?.requestAborted.record();
        return null;
      }
      if (!config) {
        Glean?.shoppingProduct?.invalidOhttpConfig.record();
        console.error(
          new Error(
            "OHTTP was configured for shopping but we couldn't get a valid config."
          )
        );
        return null;
      }
      requestPromise = lazy.ObliviousHTTP.ohttpRequest(
        ohttpRelayURL,
        config,
        apiURL,
        requestOptions
      );
    } else {
      requestPromise = fetch(apiURL, requestOptions);
    }

    let result;
    let responseOk;
    let responseStatus;
    try {
      let response = await requestPromise;
      responseOk = response.ok;
      responseStatus = response.status;
      result = await response.json();

      if (responseOk && responseSchema) {
        let validResponse = await lazy.ProductValidator.validate(
          result,
          responseSchema,
          allowValidationFailure
        );
        if (!validResponse) {
          Glean?.shoppingProduct?.invalidResponse.record();
          if (!allowValidationFailure) {
            return null;
          }
        }
      }
    } catch (error) {
      if (error.name !== "AbortError") {
        Glean?.shoppingProduct?.requestError.record();
        console.error(error);
      }
    }

    if (!responseOk && responseStatus < 500) {
      Glean?.shoppingProduct?.requestFailure.record();
    }

    // Retry 500 errors.
    if (!responseOk && responseStatus >= 500) {
      failCount++;

      Glean?.shoppingProduct?.serverFailure.record();

      // Make sure we still want to retry
      if (failCount > maxRetries) {
        Glean?.shoppingProduct?.requestRetriesFailed.record();
        return null;
      }

      Glean?.shoppingProduct?.requestRetried.record();
      // Wait for a back off timeout base on the number of failures.
      let backOff = retryTimeout * Math.pow(2, failCount - 1);

      await new Promise(resolve => lazy.setTimeout(resolve, backOff));

      // Try the request again.
      options.failCount = failCount;
      result = await ShoppingProduct.request(apiURL, bodyObj, options);
    }

    return result;
  }

  /**
   * Requests an image for a recommended product.
   *
   * @param {string} imageUrl
   * @returns {blob} A blob of the image
   */
  static async requestImageBlob(imageUrl, options = {}) {
    let { signal = new AbortController().signal } = options;
    let ohttpRelayURL = Services.prefs.getStringPref(
      "toolkit.shopping.ohttpRelayURL",
      ""
    );
    let ohttpConfigURL = Services.prefs.getStringPref(
      "toolkit.shopping.ohttpConfigURL",
      ""
    );

    let imgRequestPromise;
    if (ohttpRelayURL && ohttpConfigURL) {
      let config = await lazy.ObliviousHTTP.getOHTTPConfig(ohttpConfigURL);
      if (!config) {
        Glean?.shoppingProduct?.invalidOhttpConfig.record();
        console.error(
          new Error(
            "OHTTP was configured for shopping but we couldn't get a valid config."
          )
        );
        return null;
      }

      let imgRequestOptions = {
        signal,
        headers: {
          Accept: "image/jpeg",
          "Content-Type": "image/jpeg",
        },
        abortCallback() {
          Glean?.shoppingProduct?.requestAborted.record();
        },
      };

      imgRequestPromise = lazy.ObliviousHTTP.ohttpRequest(
        ohttpRelayURL,
        config,
        imageUrl,
        imgRequestOptions
      );
    } else {
      imgRequestPromise = fetch(imageUrl);
    }

    let imgResult;
    try {
      let response = await imgRequestPromise;
      imgResult = await response.blob();
    } catch (error) {
      if (error.name !== "AbortError") {
        Glean?.shoppingProduct?.requestError.record();
        console.error(error);
      }
    }

    return imgResult;
  }

  /**
   * The pollForAnalysisCompleted callback, called after each
   * time the progress for an analysis is received.
   *
   * @callback progressCallback
   * @param {number} progress
   *   The progress of the current analysis for a product.
   */

  /**
   * Poll Analysis Status API until an analysis has finished.
   *
   * After an initial wait keep checking the api for results,
   * until we have reached a maximum of tries.
   *
   * Passes all arguments to requestAnalysisCreationStatus().
   *
   * @example
   *  let analysis;
   *  let { status } = await product.pollForAnalysisCompleted();
   *  // Check if analysis has finished
   *  if(status != "pending" && status != "in_progress") {
   *    // Get the new analysis
   *    analysis = await product.requestAnalysis();
   *  }
   *
   * @example
   * // Check the current status
   * let { status } = await product.requestAnalysisCreationStatus();
   * if(status == "pending" && status == "in_progress") {
   *    // Start polling without the initial timeout if the analysis
   *    // is already in progress.
   *    await product.pollForAnalysisCompleted({
   *      pollInitialWait: analysisStatus == "in_progress" ? 0 : undefined,
   *    });
   * }
   * @param {object} options
   *  Override default API url and schema.
   * @param {progressCallback} [callback]
   *  Callback for analysis progress.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async pollForAnalysisCompleted(options, callback) {
    let pollCount = 0;
    let initialWait = options?.pollInitialWait || API_POLL_INITIAL_WAIT;
    let pollTimeout = options?.pollTimeout || API_POLL_WAIT;
    let pollAttempts = options?.pollAttempts || API_POLL_ATTEMPTS;
    let isFinished = false;
    let result;

    while (!isFinished && pollCount < pollAttempts) {
      if (this._abortController.signal.aborted) {
        Glean?.shoppingProduct?.requestAborted.record();
        return null;
      }
      let backOff = pollCount == 0 ? initialWait : pollTimeout;
      if (backOff) {
        await new Promise(resolve => lazy.setTimeout(resolve, backOff));
      }
      try {
        result = await this.requestAnalysisCreationStatus(undefined, options);
        if (result?.progress) {
          this.emit("analysis-progress", result.progress);
          callback && callback(result.progress);
        }
        isFinished = ShoppingProduct.hasAnalysisFinished(result?.status);
      } catch (error) {
        console.error(error);
        return null;
      }
      pollCount++;
    }
    return result;
  }

  /**
   * Request that the API creates an analysis for a product.
   *
   * Once the processing status indicates that analyzing is complete,
   * the new analysis data that can be requested with `requestAnalysis`.
   *
   * If the product is currently being analyzed, this will return a
   * status of "in_progress" and not trigger a reanalyzing the product.
   *
   * @param {Product} product
   *  Product to request for (defaults to the instances product).
   * @param {object} options
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async requestCreateAnalysis(product = this.product, options = {}) {
    let url = options?.url || ShoppingEnvironment.ANALYZE_API;
    let requestSchema = options?.requestSchema || ANALYZE_REQUEST_SCHEMA;
    let responseSchema = options?.responseSchema || ANALYZE_RESPONSE_SCHEMA;
    let signal = options?.signal || this._abortController.signal;
    let allowValidationFailure = this.allowValidationFailure;

    if (!product) {
      return null;
    }

    let requestOptions = {
      product_id: product.id,
      website: product.host,
    };

    let result = await ShoppingProduct.request(url, requestOptions, {
      requestSchema,
      responseSchema,
      signal,
      allowValidationFailure,
    });

    return result;
  }

  /**
   * Check the status of creating an analysis for a product.
   *
   * API returns a progress of 0-100 complete and the processing status.
   *
   * @param {Product} product
   *  Product to request for (defaults to the instances product).
   * @param {object} options
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async requestAnalysisCreationStatus(product = this.product, options = {}) {
    let url = options?.url || ShoppingEnvironment.ANALYSIS_STATUS_API;
    let requestSchema =
      options?.requestSchema || ANALYSIS_STATUS_REQUEST_SCHEMA;
    let responseSchema =
      options?.responseSchema || ANALYSIS_STATUS_RESPONSE_SCHEMA;
    let signal = options?.signal || this._abortController.signal;
    let allowValidationFailure = this.allowValidationFailure;

    if (!product) {
      return null;
    }

    let requestOptions = {
      product_id: product.id,
      website: product.host,
    };

    let result = await ShoppingProduct.request(url, requestOptions, {
      requestSchema,
      responseSchema,
      signal,
      allowValidationFailure,
    });

    return result;
  }

  /**
   * Send an event to the Ad Attribution API
   *
   * @param {string} eventName
   *  Event name options are:
   *  - "impression"
   *  - "click"
   * @param {string} aid
   *  The aid (Ad ID) from the recommendation.
   * @param {string} [source]
   *  Source of the event
   * @param {object} [options]
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  static async sendAttributionEvent(
    eventName,
    aid,
    source = "firefox_sidebar",
    options = {}
  ) {
    let {
      url = ShoppingEnvironment.ATTRIBUTION_API,
      requestSchema = ATTRIBUTION_REQUEST_SCHEMA,
      responseSchema = ATTRIBUTION_RESPONSE_SCHEMA,
      signal = new AbortController().signal,
      allowValidationFailure = true,
    } = options;

    if (!eventName) {
      throw new Error("An event name is required.");
    }
    if (!aid) {
      throw new Error("An Ad ID is required.");
    }

    let requestBody = {
      event_source: source,
    };

    switch (eventName) {
      case "impression":
        requestBody.event_name = "trusted_deals_impression";
        requestBody.aidvs = [aid];
        break;
      case "click":
        requestBody.event_name = "trusted_deals_link_clicked";
        requestBody.aid = aid;
        break;
      case "placement":
        requestBody.event_name = "trusted_deals_placement";
        requestBody.aidvs = [aid];
        break;
      default:
        throw new Error(`"${eventName}" is not a valid event name`);
    }

    let result = await ShoppingProduct.request(url, requestBody, {
      requestSchema,
      responseSchema,
      signal,
      allowValidationFailure,
    });

    return result;
  }

  /**
   * Send a report that a product is back in stock.
   *
   * @param {Product} product
   *  Product to request for (defaults to the instances product).
   * @param {object} options
   *  Override default API url and schema.
   * @returns {object} result
   *  Parsed JSON API result or null.
   */
  async sendReport(product = this.product, options = {}) {
    if (!product) {
      return null;
    }

    let url = options?.url || ShoppingEnvironment.REPORTING_API;
    let requestSchema = options?.requestSchema || REPORTING_REQUEST_SCHEMA;
    let responseSchema = options?.responseSchema || REPORTING_RESPONSE_SCHEMA;
    let signal = options?.signal || this._abortController.signal;
    let allowValidationFailure = this.allowValidationFailure;

    let requestOptions = {
      product_id: product.id,
      website: product.host,
    };

    let result = await ShoppingProduct.request(url, requestOptions, {
      requestSchema,
      responseSchema,
      signal,
      allowValidationFailure,
    });

    return result;
  }

  uninit() {
    this._abortController.abort();
    this.product = null;
  }
}

/**
 * Check if a URL is a valid product.
 *
 * @param {URL | nsIURI } url
 *  URL to check.
 * @returns {boolean}
 */
export function isProductURL(url) {
  if (url instanceof Ci.nsIURI) {
    url = URL.fromURI(url);
  }
  if (!URL.isInstance(url)) {
    return false;
  }
  let productInfo = ShoppingProduct.fromURL(url);
  return ShoppingProduct.isProduct(productInfo);
}

/**
 * Check if a URL is a valid product site.
 *
 * @param {URL | nsIURI } url
 *  URL to check.
 * @returns {boolean}
 */
export function isSupportedSiteURL(url) {
  if (url instanceof Ci.nsIURI) {
    url = URL.fromURI(url);
  }
  if (!URL.isInstance(url)) {
    return false;
  }
  let productInfo = ShoppingProduct.fromURL(url);
  return ShoppingProduct.isSupportedSite(productInfo);
}

/**
 * Get the product ID of a valid product URL.
 *
 * @param {URL | nsIURI } url
 *  URL to check.
 * @returns {number} id
 */
export function getProductIdFromURL(url) {
  if (url instanceof Ci.nsIURI) {
    url = URL.fromURI(url);
  }
  if (!URL.isInstance(url)) {
    return null;
  }

  let productInfo = ShoppingProduct.fromURL(url);
  if (!ShoppingProduct.isProduct(productInfo)) {
    return null;
  }

  return productInfo.id;
}
