/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import ReactDOM from "react-dom";
import { AboutWelcomeUtils } from "./lib/aboutwelcome-utils.mjs";
import { MultiStageAboutWelcome } from "./components/MultiStageAboutWelcome";

class AboutWelcome extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = { metricsFlowUri: null };
    this.fetchFxAFlowUri = this.fetchFxAFlowUri.bind(this);
  }

  async fetchFxAFlowUri() {
    this.setState({ metricsFlowUri: await window.AWGetFxAMetricsFlowURI?.() });
  }

  componentDidMount() {
    if (!this.props.skipFxA) {
      this.fetchFxAFlowUri();
    }

    if (document.location.href === "about:welcome") {
      // Record impression with performance data after allowing the page to load
      const recordImpression = domState => {
        const { domComplete, domInteractive } = performance
          .getEntriesByType("navigation")
          .pop();
        AboutWelcomeUtils.sendImpressionTelemetry(this.props.messageId, {
          domComplete,
          domInteractive,
          mountStart: performance.getEntriesByName("mount").pop().startTime,
          domState,
          source: this.props.UTMTerm,
        });
      };
      if (document.readyState === "complete") {
        // Page might have already triggered a load event because it waited for async data,
        // e.g., attribution, so the dom load timing could be of a empty content
        // with domState in telemetry captured as 'complete'
        recordImpression(document.readyState);
      } else {
        window.addEventListener("load", () => recordImpression("load"), {
          once: true,
        });
      }

      // Captures user has seen about:welcome by setting
      // firstrun.didSeeAboutWelcome pref to true and capturing welcome UI unique messageId
      window.AWSendToParent("SET_WELCOME_MESSAGE_SEEN", this.props.messageId);
    }
  }

  render() {
    const { props } = this;
    return (
      <MultiStageAboutWelcome
        addonId={props.addonId}
        addonType={props.type}
        addonName={props.name || ""}
        addonURL={props.url}
        addonIconURL={props.iconURL}
        themeScreenshots={props.screenshots}
        message_id={props.messageId}
        defaultScreens={props.screens}
        updateHistory={!props.disableHistoryUpdates}
        metricsFlowUri={this.state.metricsFlowUri}
        utm_term={props.UTMTerm}
        transitions={props.transitions}
        backdrop={props.backdrop}
        startScreen={props.startScreen || 0}
        appAndSystemLocaleInfo={props.appAndSystemLocaleInfo}
        ariaRole={props.aria_role}
      />
    );
  }
}

// Computes messageId and UTMTerm info used in telemetry
function ComputeTelemetryInfo(welcomeContent, experimentId, branchId) {
  let messageId =
    welcomeContent.template === "return_to_amo"
      ? `RTAMO_DEFAULT_WELCOME_${welcomeContent.type.toUpperCase()}`
      : "DEFAULT_ID";
  let UTMTerm = "aboutwelcome-default";

  if (welcomeContent.id) {
    messageId = welcomeContent.id.toUpperCase();
  }

  if (experimentId && branchId) {
    UTMTerm = `aboutwelcome-${experimentId}-${branchId}`.toLowerCase();
  }
  return {
    messageId,
    UTMTerm,
  };
}

async function retrieveRenderContent() {
  // Feature config includes RTAMO attribution data if exists
  // else below data in order specified
  // user prefs
  // experiment data
  // defaults
  let featureConfig = await window.AWGetFeatureConfig();

  let { messageId, UTMTerm } = ComputeTelemetryInfo(
    featureConfig,
    featureConfig.slug,
    featureConfig.branch && featureConfig.branch.slug
  );
  return { featureConfig, messageId, UTMTerm };
}

async function mount() {
  let {
    featureConfig: aboutWelcomeProps,
    messageId,
    UTMTerm,
  } = await retrieveRenderContent();
  ReactDOM.render(
    <AboutWelcome
      messageId={messageId}
      UTMTerm={UTMTerm}
      {...aboutWelcomeProps}
    />,
    document.getElementById("multi-stage-message-root")
  );
}

performance.mark("mount");
mount();
