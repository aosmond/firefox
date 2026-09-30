/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPMonitorInfoWin_h_
#define GMPMonitorInfoWin_h_

#include <windows.h>

#include "mozilla/gmp/GMPTypes.h"
#include "nsTArray.h"

namespace mozilla::gmp {

// Collects the monitor information from the parent process to send to GMP
// processes which cannot make win32k system calls.
nsTArray<GMPMonitorInfo> CollectMonitorInfo();

// Replaces the monitor information used to service the hooked monitor APIs in
// the GMP process.
void SetMonitorInfo(nsTArray<GMPMonitorInfo>&& aMonitors);

// Implementations of the monitor APIs using the monitor information provided
// by SetMonitorInfo. These are used by the hooks installed by
// InitializeMonitorHooks, and are exposed for testing.
BOOL WINAPI CachedEnumDisplayMonitors(HDC aDC, LPCRECT aClip,
                                      MONITORENUMPROC aCallback, LPARAM aData);
BOOL WINAPI CachedGetMonitorInfoW(HMONITOR aMonitor, LPMONITORINFO aInfo);
BOOL WINAPI CachedGetMonitorInfoA(HMONITOR aMonitor, LPMONITORINFO aInfo);

// Hooks the monitor APIs in the GMP process so that they are serviced from the
// monitor information provided by the parent process, since win32k system
// calls are unavailable.
void InitializeMonitorHooks();

}  // namespace mozilla::gmp

#endif  // GMPMonitorInfoWin_h_
