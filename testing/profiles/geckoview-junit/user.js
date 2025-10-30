/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Base preferences file used by the mochitest
/* globals user_pref */
/* eslint quotes: 0 */

// Turn off update
user_pref("app.update.disabledForTesting", true);

// Allow the GPU process to be launched in the background, for which
// many tests run in but still require compositing.
user_pref("layers.gpu-process.launch-in-background", true);
