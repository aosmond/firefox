/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include "GMPMitigationPolicyWin.h"
#include "gtest/gtest.h"

using namespace mozilla::gmp;

static const void* AddressInNtdll() {
  return reinterpret_cast<const char*>(::GetModuleHandleW(L"ntdll.dll")) +
         0x1000;
}

static void FunctionOutsideNtdll() {}

TEST(GMPMitigationPolicyWin, SpoofClearsWin32kDisallowed)
{
  PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY policy{};
  policy.DisallowWin32kSystemCalls = 1;
  policy.AuditDisallowWin32kSystemCalls = 1;

  SpoofMitigationPolicy(ProcessSystemCallDisablePolicy, &policy,
                        sizeof(policy));
  EXPECT_EQ(policy.DisallowWin32kSystemCalls, 0u);
  EXPECT_EQ(policy.AuditDisallowWin32kSystemCalls, 1u);
}

TEST(GMPMitigationPolicyWin, SpoofIgnoresOtherPolicies)
{
  PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy{};
  policy.Flags = 0xffffffff;

  SpoofMitigationPolicy(ProcessDynamicCodePolicy, &policy, sizeof(policy));
  EXPECT_EQ(policy.Flags, 0xffffffffu);
}

TEST(GMPMitigationPolicyWin, SpoofIgnoresInvalidBuffers)
{
  PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY policy{};
  policy.DisallowWin32kSystemCalls = 1;

  SpoofMitigationPolicy(ProcessSystemCallDisablePolicy, &policy, 0);
  EXPECT_EQ(policy.DisallowWin32kSystemCalls, 1u);

  SpoofMitigationPolicy(ProcessSystemCallDisablePolicy, nullptr,
                        sizeof(policy));
}

TEST(GMPMitigationPolicyWin, ShouldSpoofForCallerInModule)
{
  EXPECT_TRUE(ShouldSpoofMitigationPolicy(L"ntdll.dll", AddressInNtdll(),
                                          ::GetCurrentProcess()));
}

TEST(GMPMitigationPolicyWin, ShouldSpoofForRealCurrentProcessHandle)
{
  HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 ::GetCurrentProcessId());
  ASSERT_TRUE(process);
  EXPECT_TRUE(
      ShouldSpoofMitigationPolicy(L"ntdll.dll", AddressInNtdll(), process));
  ::CloseHandle(process);
}

TEST(GMPMitigationPolicyWin, ShouldNotSpoofForCallerOutsideModule)
{
  EXPECT_FALSE(ShouldSpoofMitigationPolicy(
      L"ntdll.dll", reinterpret_cast<const void*>(&FunctionOutsideNtdll),
      ::GetCurrentProcess()));
}

TEST(GMPMitigationPolicyWin, ShouldNotSpoofForModuleNotLoaded)
{
  EXPECT_FALSE(ShouldSpoofMitigationPolicy(L"widevinecdm.dll", AddressInNtdll(),
                                           ::GetCurrentProcess()));
}

TEST(GMPMitigationPolicyWin, ShouldNotSpoofForInvalidArguments)
{
  EXPECT_FALSE(ShouldSpoofMitigationPolicy(nullptr, AddressInNtdll(),
                                           ::GetCurrentProcess()));
  EXPECT_FALSE(ShouldSpoofMitigationPolicy(L"ntdll.dll", nullptr,
                                           ::GetCurrentProcess()));
  EXPECT_FALSE(
      ShouldSpoofMitigationPolicy(L"ntdll.dll", AddressInNtdll(), nullptr));
}
