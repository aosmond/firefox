/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMitigationPolicyWin_h_
#define GMPMitigationPolicyWin_h_

#include <windows.h>

namespace mozilla::gmp {

// Whether a GetProcessMitigationPolicy query made from aCaller for aProcess
// should be spoofed, i.e. aCaller is within the module named aModuleName and
// aProcess is the current process.
bool ShouldSpoofMitigationPolicy(const wchar_t* aModuleName,
                                 const void* aCaller, HANDLE aProcess);

// Adjusts the result of a GetProcessMitigationPolicy query to report that
// win32k system calls are permitted.
void SpoofMitigationPolicy(PROCESS_MITIGATION_POLICY aPolicy, PVOID aBuffer,
                           SIZE_T aLength);

// Hooks GetProcessMitigationPolicy in the GMP process so that queries made
// from the module named aModuleName report that win32k system calls are
// permitted. Some plugins, such as Widevine, refuse to run if they detect that
// win32k system calls are disabled, even when they do not need them. The
// module name must have static storage duration.
void InitializeMitigationPolicyHook(const wchar_t* aModuleName);

}  // namespace mozilla::gmp

#endif  // GMPMitigationPolicyWin_h_
