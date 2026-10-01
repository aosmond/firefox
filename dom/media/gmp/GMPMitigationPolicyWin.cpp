/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPMitigationPolicyWin.h"

#include <intrin.h>
#include <windows.h>

#include "GMPLog.h"
#include "nsWindowsDllInterceptor.h"

namespace mozilla::gmp {

bool ShouldSpoofMitigationPolicy(const wchar_t* aModuleName,
                                 const void* aCaller, HANDLE aProcess) {
  if (!aModuleName || !aCaller ||
      ::GetProcessId(aProcess) != ::GetCurrentProcessId()) {
    return false;
  }

  HMODULE module = ::GetModuleHandleW(aModuleName);
  if (!module) {
    return false;
  }

  HMODULE callerModule = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(aCaller), &callerModule)) {
    return false;
  }

  return callerModule == module;
}

void SpoofMitigationPolicy(PROCESS_MITIGATION_POLICY aPolicy, PVOID aBuffer,
                           SIZE_T aLength) {
  if (aPolicy != ProcessSystemCallDisablePolicy || !aBuffer ||
      aLength < sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) {
    return;
  }

  auto* policy =
      static_cast<PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY>(aBuffer);
  policy->DisallowWin32kSystemCalls = 0;
}

MOZ_RUNINIT static WindowsDllInterceptor sKernelBaseIntercept;

static WindowsDllInterceptor::FuncHookType<
    decltype(&::GetProcessMitigationPolicy)>
    sOriginalGetProcessMitigationPolicy;

static const wchar_t* sSpoofModuleName = nullptr;

static BOOL WINAPI GetProcessMitigationPolicyHook(
    HANDLE aProcess, PROCESS_MITIGATION_POLICY aPolicy, PVOID aBuffer,
    SIZE_T aLength) {
  if (!sOriginalGetProcessMitigationPolicy(aProcess, aPolicy, aBuffer,
                                           aLength)) {
    return FALSE;
  }

  if (aPolicy == ProcessSystemCallDisablePolicy &&
      ShouldSpoofMitigationPolicy(sSpoofModuleName, _ReturnAddress(),
                                  aProcess)) {
    GMP_LOG_DEBUG("GetProcessMitigationPolicyHook: spoofing win32k policy");
    SpoofMitigationPolicy(aPolicy, aBuffer, aLength);
  }
  return TRUE;
}

void InitializeMitigationPolicyHook(const wchar_t* aModuleName) {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  sSpoofModuleName = aModuleName;
  sKernelBaseIntercept.Init("kernelbase.dll");
  if (!sOriginalGetProcessMitigationPolicy.Set(
          sKernelBaseIntercept, "GetProcessMitigationPolicy",
          &GetProcessMitigationPolicyHook)) {
    NS_WARNING("Failed to hook GetProcessMitigationPolicy");
  }
}

}  // namespace mozilla::gmp
