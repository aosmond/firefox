/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPMonitorInfoWin.h"

#include <windows.h>

#include <algorithm>

#include "GMPLog.h"
#include "mozilla/RWLock.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "nsWindowsDllInterceptor.h"

namespace mozilla::gmp {

static gfx::IntRect ToIntRect(const RECT& aRect) {
  return gfx::IntRect(aRect.left, aRect.top, aRect.right - aRect.left,
                      aRect.bottom - aRect.top);
}

static RECT ToRECT(const gfx::IntRect& aRect) {
  return RECT{aRect.X(), aRect.Y(), aRect.XMost(), aRect.YMost()};
}

static BOOL CALLBACK CollectMonitor(HMONITOR aMonitor, HDC, LPRECT,
                                    LPARAM aParam) {
  auto* monitors = reinterpret_cast<nsTArray<GMPMonitorInfo>*>(aParam);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!::GetMonitorInfoW(aMonitor, &info)) {
    GMP_LOG_DEBUG("CollectMonitor: GetMonitorInfoW failed");
    return TRUE;
  }

  monitors->AppendElement(GMPMonitorInfo(
      reinterpret_cast<uintptr_t>(aMonitor), ToIntRect(info.rcMonitor),
      ToIntRect(info.rcWork), info.dwFlags, nsString(info.szDevice)));
  return TRUE;
}

nsTArray<GMPMonitorInfo> CollectMonitorInfo() {
  nsTArray<GMPMonitorInfo> monitors;
  if (!::EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
                             reinterpret_cast<LPARAM>(&monitors))) {
    GMP_LOG_DEBUG("CollectMonitorInfo: EnumDisplayMonitors failed");
  }
  return monitors;
}

using DdQueryDisplaySettingsUniquenessFn = ULONG(WINAPI*)();

// gdi32 does not export DdQueryDisplaySettingsUniqueness by name, only as the
// DirectDraw entry point GdiEntry13, per ddrawgdi.h.
static constexpr char kDdQueryDisplaySettingsUniquenessExport[] = "GdiEntry13";

uint32_t QueryDisplaySettingsUniqueness() {
  static const auto sQuery =
      reinterpret_cast<DdQueryDisplaySettingsUniquenessFn>(
          ::GetProcAddress(::GetModuleHandleW(L"gdi32.dll"),
                           kDdQueryDisplaySettingsUniquenessExport));
  if (!sQuery) {
    GMP_LOG_DEBUG("QueryDisplaySettingsUniqueness: unavailable");
    return 0;
  }
  return sQuery();
}

namespace {

struct CachedMonitor {
  HMONITOR mHandle;
  RECT mMonitorRect;
  RECT mWorkRect;
  DWORD mFlags;
  WCHAR mDeviceName[CCHDEVICENAME];
};

}  // namespace

static StaticRWLock sMonitorLock;
static StaticAutoPtr<nsTArray<CachedMonitor>> sMonitors;
static uint32_t sDisplaySettingsUniqueness = 0;

void SetMonitorInfo(nsTArray<GMPMonitorInfo>&& aMonitors,
                    uint32_t aDisplaySettingsUniqueness) {
  auto monitors = MakeUnique<nsTArray<CachedMonitor>>(aMonitors.Length());
  for (const auto& monitor : aMonitors) {
    CachedMonitor* cached = monitors->AppendElement();
    cached->mHandle = reinterpret_cast<HMONITOR>(monitor.mHandle());
    cached->mMonitorRect = ToRECT(monitor.mMonitorRect());
    cached->mWorkRect = ToRECT(monitor.mWorkRect());
    cached->mFlags = monitor.mFlags();
    wcsncpy_s(cached->mDeviceName, monitor.mDeviceName().get(), _TRUNCATE);
  }

  GMP_LOG_DEBUG("SetMonitorInfo: {} monitors, uniqueness {}",
                monitors->Length(), aDisplaySettingsUniqueness);
  StaticAutoWriteLock lock(sMonitorLock);
  sMonitors = monitors.release();
  sDisplaySettingsUniqueness = aDisplaySettingsUniqueness;
}

ULONG WINAPI CachedDdQueryDisplaySettingsUniqueness() {
  StaticAutoReadLock lock(sMonitorLock);
  return sDisplaySettingsUniqueness;
}

MOZ_RUNINIT static WindowsDllInterceptor sUser32Intercept;

static WindowsDllInterceptor::FuncHookType<decltype(&::EnumDisplayMonitors)>
    sOriginalEnumDisplayMonitors;
static WindowsDllInterceptor::FuncHookType<decltype(&::GetMonitorInfoW)>
    sOriginalGetMonitorInfoW;
static WindowsDllInterceptor::FuncHookType<decltype(&::GetMonitorInfoA)>
    sOriginalGetMonitorInfoA;

MOZ_RUNINIT static WindowsDllInterceptor sGdi32Intercept;

static WindowsDllInterceptor::FuncHookType<DdQueryDisplaySettingsUniquenessFn>
    sOriginalDdQueryDisplaySettingsUniqueness;

BOOL WINAPI CachedEnumDisplayMonitors(HDC aDC, LPCRECT aClip,
                                      MONITORENUMPROC aCallback, LPARAM aData) {
  // We cannot determine the visible region of a device context without win32k.
  if (aDC || !aCallback) {
    GMP_LOG_DEBUG("CachedEnumDisplayMonitors: unsupported arguments");
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  // Copy the monitors so that we don't hold the lock during the callbacks.
  AutoTArray<CachedMonitor, 4> monitors;
  {
    StaticAutoReadLock lock(sMonitorLock);
    if (sMonitors) {
      monitors.AppendElements(*sMonitors);
    }
  }

  for (const auto& monitor : monitors) {
    RECT rect = monitor.mMonitorRect;
    if (aClip) {
      rect.left = std::max(rect.left, aClip->left);
      rect.top = std::max(rect.top, aClip->top);
      rect.right = std::min(rect.right, aClip->right);
      rect.bottom = std::min(rect.bottom, aClip->bottom);
      if (rect.left >= rect.right || rect.top >= rect.bottom) {
        continue;
      }
    }

    if (!aCallback(monitor.mHandle, nullptr, &rect, aData)) {
      break;
    }
  }

  return TRUE;
}

// Finds the cached monitor for aMonitor and fills in the fields common to
// MONITORINFO and its extended variants. sMonitorLock must be held.
static const CachedMonitor* FillMonitorInfo(HMONITOR aMonitor,
                                            LPMONITORINFO aInfo) {
  if (!sMonitors) {
    return nullptr;
  }

  for (const auto& monitor : *sMonitors) {
    if (monitor.mHandle == aMonitor) {
      aInfo->rcMonitor = monitor.mMonitorRect;
      aInfo->rcWork = monitor.mWorkRect;
      aInfo->dwFlags = monitor.mFlags;
      return &monitor;
    }
  }

  return nullptr;
}

BOOL WINAPI CachedGetMonitorInfoW(HMONITOR aMonitor, LPMONITORINFO aInfo) {
  if (!aInfo || (aInfo->cbSize != sizeof(MONITORINFO) &&
                 aInfo->cbSize != sizeof(MONITORINFOEXW))) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  StaticAutoReadLock lock(sMonitorLock);
  const CachedMonitor* monitor = FillMonitorInfo(aMonitor, aInfo);
  if (!monitor) {
    GMP_LOG_DEBUG("CachedGetMonitorInfoW: unknown monitor");
    ::SetLastError(ERROR_INVALID_MONITOR_HANDLE);
    return FALSE;
  }

  if (aInfo->cbSize == sizeof(MONITORINFOEXW)) {
    auto* infoEx = reinterpret_cast<LPMONITORINFOEXW>(aInfo);
    wcsncpy_s(infoEx->szDevice, monitor->mDeviceName, _TRUNCATE);
  }
  return TRUE;
}

BOOL WINAPI CachedGetMonitorInfoA(HMONITOR aMonitor, LPMONITORINFO aInfo) {
  if (!aInfo || (aInfo->cbSize != sizeof(MONITORINFO) &&
                 aInfo->cbSize != sizeof(MONITORINFOEXA))) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  StaticAutoReadLock lock(sMonitorLock);
  const CachedMonitor* monitor = FillMonitorInfo(aMonitor, aInfo);
  if (!monitor) {
    GMP_LOG_DEBUG("CachedGetMonitorInfoA: unknown monitor");
    ::SetLastError(ERROR_INVALID_MONITOR_HANDLE);
    return FALSE;
  }

  if (aInfo->cbSize == sizeof(MONITORINFOEXA)) {
    auto* infoEx = reinterpret_cast<LPMONITORINFOEXA>(aInfo);
    if (!::WideCharToMultiByte(CP_ACP, 0, monitor->mDeviceName, -1,
                               infoEx->szDevice, sizeof(infoEx->szDevice),
                               nullptr, nullptr)) {
      infoEx->szDevice[0] = '\0';
    }
  }
  return TRUE;
}

void InitializeMonitorHooks() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  sUser32Intercept.Init("user32.dll");
  if (!sOriginalEnumDisplayMonitors.Set(sUser32Intercept, "EnumDisplayMonitors",
                                        &CachedEnumDisplayMonitors)) {
    NS_WARNING("Failed to hook EnumDisplayMonitors");
  }
  if (!sOriginalGetMonitorInfoW.Set(sUser32Intercept, "GetMonitorInfoW",
                                    &CachedGetMonitorInfoW)) {
    NS_WARNING("Failed to hook GetMonitorInfoW");
  }
  if (!sOriginalGetMonitorInfoA.Set(sUser32Intercept, "GetMonitorInfoA",
                                    &CachedGetMonitorInfoA)) {
    NS_WARNING("Failed to hook GetMonitorInfoA");
  }

  // This reads from memory shared with win32k, which is not mapped into the
  // process when win32k system calls are disabled.
  sGdi32Intercept.Init("gdi32.dll");
  if (!sOriginalDdQueryDisplaySettingsUniqueness.Set(
          sGdi32Intercept, kDdQueryDisplaySettingsUniquenessExport,
          &CachedDdQueryDisplaySettingsUniqueness)) {
    NS_WARNING("Failed to hook DdQueryDisplaySettingsUniqueness");
  }
}

}  // namespace mozilla::gmp
