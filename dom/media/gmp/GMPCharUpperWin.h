/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPCharUpperWin_h_
#define GMPCharUpperWin_h_

#include <windows.h>

namespace mozilla::gmp {

// Implementation of CharUpperW using locale invariant case mapping, which does
// not depend upon user32 or win32k. Exposed for testing.
LPWSTR WINAPI LocaleInvariantCharUpperW(LPWSTR aString);

// Hooks CharUpperW in the GMP process so that it does not depend upon user32
// being usable, since win32k system calls are unavailable.
void InitializeCharUpperHook();

}  // namespace mozilla::gmp

#endif  // GMPCharUpperWin_h_
