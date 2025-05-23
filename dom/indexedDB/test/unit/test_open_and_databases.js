/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* exported testSteps */
async function testSteps() {
  const factory = (function () {
    if (typeof Cc === "undefined") {
      return indexedDB;
    }

    // In xpcshell tests (where `Cc` is defined), we avoid using the system
    // principal because background services may create indexedDB databases
    // concurrently under that principal. To prevent interference and ensure
    // test isolation, we use a sandbox with a content principal instead.

    const { PrincipalUtils } = ChromeUtils.importESModule(
      "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
    );

    const principal = PrincipalUtils.createPrincipal("https://example.com");

    const sandbox = new Cu.Sandbox(principal, {
      wantGlobalProperties: ["indexedDB"],
    });

    return Cu.evalInSandbox("indexedDB", sandbox);
  })();

  const openInfos = [
    { name: "foo-a", version: 1 },
    { name: "foo-b", version: 1 },
  ];

  info("Creating databases");

  for (let index = 0; index < openInfos.length; index++) {
    const openInfo = openInfos[index];

    const request = factory.open(openInfo.name, openInfo.version);

    await expectingUpgrade(request);

    const event = await expectingSuccess(request);

    const database = event.target.result;

    database.close();
  }

  info("Getting databases");

  const databasesPromise = factory.databases();

  info("Opening databases");

  const openPromises = [];

  for (let index = 0; index < openInfos.length; index++) {
    const openInfo = openInfos[index];

    const request = factory.open(openInfo.name, openInfo.version);

    const promise = expectingSuccess(request);

    openPromises.push(promise);
  }

  info("Waiting for databases operation to complete");

  const databaseInfos = await databasesPromise;

  info("Verifying databases");

  is(
    databaseInfos.length,
    openInfos.length,
    "The result of databases() should contain one result per database"
  );

  for (let index = 0; index < openInfos.length; index++) {
    const openInfo = openInfos[index];

    ok(
      databaseInfos.some(function (element) {
        return (
          element.name === openInfo.name && element.version === openInfo.version
        );
      }),
      "The result of databases() should be a sequence of the correct names " +
        "and versions of all databases for the origin"
    );
  }

  info("Waiting for open operations to complete");

  await Promise.all(openPromises);
}
