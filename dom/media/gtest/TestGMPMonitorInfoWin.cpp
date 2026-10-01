/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include "GMPMonitorInfoWin.h"
#include "gtest/gtest.h"
#include "nsString.h"
#include "nsTArray.h"

using namespace mozilla;
using namespace mozilla::gmp;

static const HMONITOR kPrimary = reinterpret_cast<HMONITOR>(0x1001);
static const HMONITOR kSecondary = reinterpret_cast<HMONITOR>(0x1002);

static nsTArray<GMPMonitorInfo> MakeMonitors() {
  nsTArray<GMPMonitorInfo> monitors;
  monitors.AppendElement(GMPMonitorInfo(
      reinterpret_cast<uintptr_t>(kPrimary), gfx::IntRect(0, 0, 1920, 1080),
      gfx::IntRect(0, 0, 1920, 1040), MONITORINFOF_PRIMARY,
      u"\\\\.\\DISPLAY1"_ns));
  monitors.AppendElement(GMPMonitorInfo(reinterpret_cast<uintptr_t>(kSecondary),
                                        gfx::IntRect(1920, 0, 2560, 1440),
                                        gfx::IntRect(1920, 0, 2560, 1400), 0,
                                        u"\\\\.\\DISPLAY2"_ns));
  return monitors;
}

static void ExpectRect(const RECT& aRect, LONG aLeft, LONG aTop, LONG aRight,
                       LONG aBottom) {
  EXPECT_EQ(aRect.left, aLeft);
  EXPECT_EQ(aRect.top, aTop);
  EXPECT_EQ(aRect.right, aRight);
  EXPECT_EQ(aRect.bottom, aBottom);
}

static void ExpectSameRect(const RECT& aActual, const RECT& aExpected) {
  ExpectRect(aActual, aExpected.left, aExpected.top, aExpected.right,
             aExpected.bottom);
}

namespace {

struct EnumeratedMonitor {
  HMONITOR mMonitor;
  HDC mDC;
  RECT mRect;
};

struct EnumerateState {
  nsTArray<EnumeratedMonitor> mMonitors;
  size_t mStopAfter = SIZE_MAX;
};

}  // namespace

static BOOL CALLBACK RecordMonitor(HMONITOR aMonitor, HDC aDC, LPRECT aRect,
                                   LPARAM aParam) {
  auto* state = reinterpret_cast<EnumerateState*>(aParam);
  state->mMonitors.AppendElement(EnumeratedMonitor{aMonitor, aDC, *aRect});
  return state->mMonitors.Length() < state->mStopAfter;
}

TEST(GMPMonitorInfoWin, EnumerateAll)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EnumerateState state;
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  ASSERT_EQ(state.mMonitors.Length(), 2u);

  EXPECT_EQ(state.mMonitors[0].mMonitor, kPrimary);
  EXPECT_EQ(state.mMonitors[0].mDC, nullptr);
  ExpectRect(state.mMonitors[0].mRect, 0, 0, 1920, 1080);

  EXPECT_EQ(state.mMonitors[1].mMonitor, kSecondary);
  EXPECT_EQ(state.mMonitors[1].mDC, nullptr);
  ExpectRect(state.mMonitors[1].mRect, 1920, 0, 4480, 1440);
}

TEST(GMPMonitorInfoWin, EnumerateClipped)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EnumerateState state;
  RECT clip{1800, 100, 2000, 200};
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, &clip, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  ASSERT_EQ(state.mMonitors.Length(), 2u);
  EXPECT_EQ(state.mMonitors[0].mMonitor, kPrimary);
  ExpectRect(state.mMonitors[0].mRect, 1800, 100, 1920, 200);
  EXPECT_EQ(state.mMonitors[1].mMonitor, kSecondary);
  ExpectRect(state.mMonitors[1].mRect, 1920, 100, 2000, 200);
}

TEST(GMPMonitorInfoWin, EnumerateClippedExcludesAdjacentMonitor)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EnumerateState state;
  RECT clip{1920, 0, 1930, 10};
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, &clip, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  ASSERT_EQ(state.mMonitors.Length(), 1u);
  EXPECT_EQ(state.mMonitors[0].mMonitor, kSecondary);
  ExpectRect(state.mMonitors[0].mRect, 1920, 0, 1930, 10);
}

TEST(GMPMonitorInfoWin, EnumerateStopsWhenCallbackFails)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EnumerateState state;
  state.mStopAfter = 1;
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  ASSERT_EQ(state.mMonitors.Length(), 1u);
  EXPECT_EQ(state.mMonitors[0].mMonitor, kPrimary);
}

TEST(GMPMonitorInfoWin, EnumerateEmpty)
{
  SetMonitorInfo({}, 1);

  EnumerateState state;
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  EXPECT_TRUE(state.mMonitors.IsEmpty());
}

TEST(GMPMonitorInfoWin, EnumerateRejectsUnsupportedArguments)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EnumerateState state;
  EXPECT_FALSE(CachedEnumDisplayMonitors(reinterpret_cast<HDC>(0x1), nullptr,
                                         RecordMonitor,
                                         reinterpret_cast<LPARAM>(&state)));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
  EXPECT_TRUE(state.mMonitors.IsEmpty());

  EXPECT_FALSE(CachedEnumDisplayMonitors(nullptr, nullptr, nullptr, 0));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
}

TEST(GMPMonitorInfoWin, GetMonitorInfo)
{
  SetMonitorInfo(MakeMonitors(), 1);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  EXPECT_TRUE(CachedGetMonitorInfoW(kSecondary, &info));
  ExpectRect(info.rcMonitor, 1920, 0, 4480, 1440);
  ExpectRect(info.rcWork, 1920, 0, 4480, 1400);
  EXPECT_EQ(info.dwFlags, 0u);

  info = {};
  info.cbSize = sizeof(info);
  EXPECT_TRUE(CachedGetMonitorInfoA(kPrimary, &info));
  ExpectRect(info.rcMonitor, 0, 0, 1920, 1080);
  ExpectRect(info.rcWork, 0, 0, 1920, 1040);
  EXPECT_EQ(info.dwFlags, static_cast<DWORD>(MONITORINFOF_PRIMARY));
}

TEST(GMPMonitorInfoWin, GetMonitorInfoEx)
{
  SetMonitorInfo(MakeMonitors(), 1);

  MONITORINFOEXW infoW{};
  infoW.cbSize = sizeof(infoW);
  EXPECT_TRUE(CachedGetMonitorInfoW(kSecondary, &infoW));
  ExpectRect(infoW.rcMonitor, 1920, 0, 4480, 1440);
  EXPECT_STREQ(infoW.szDevice, L"\\\\.\\DISPLAY2");

  MONITORINFOEXA infoA{};
  infoA.cbSize = sizeof(infoA);
  EXPECT_TRUE(CachedGetMonitorInfoA(kSecondary, &infoA));
  ExpectRect(infoA.rcMonitor, 1920, 0, 4480, 1440);
  EXPECT_STREQ(infoA.szDevice, "\\\\.\\DISPLAY2");
}

TEST(GMPMonitorInfoWin, GetMonitorInfoTruncatesDeviceName)
{
  nsString longName;
  for (size_t i = 0; i < CCHDEVICENAME + 8; ++i) {
    longName.Append(char16_t(u'a' + (i % 26)));
  }

  nsTArray<GMPMonitorInfo> monitors;
  monitors.AppendElement(GMPMonitorInfo(
      reinterpret_cast<uintptr_t>(kPrimary), gfx::IntRect(0, 0, 100, 100),
      gfx::IntRect(0, 0, 100, 100), 0, longName));
  SetMonitorInfo(std::move(monitors), 1);

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  EXPECT_TRUE(CachedGetMonitorInfoW(kPrimary, &info));
  EXPECT_EQ(wcslen(info.szDevice), static_cast<size_t>(CCHDEVICENAME - 1));
  EXPECT_TRUE(Substring(longName, 0, CCHDEVICENAME - 1)
                  .Equals(reinterpret_cast<const char16_t*>(info.szDevice)));
}

TEST(GMPMonitorInfoWin, GetMonitorInfoRejectsInvalidArguments)
{
  SetMonitorInfo(MakeMonitors(), 1);

  EXPECT_FALSE(CachedGetMonitorInfoW(kPrimary, nullptr));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
  EXPECT_FALSE(CachedGetMonitorInfoA(kPrimary, nullptr));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));

  MONITORINFOEXW info{};
  info.cbSize = 0;
  EXPECT_FALSE(CachedGetMonitorInfoW(kPrimary, &info));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));

  // The ANSI and wide extended structures have different sizes.
  info.cbSize = sizeof(MONITORINFOEXW);
  EXPECT_FALSE(CachedGetMonitorInfoA(kPrimary, &info));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
}

TEST(GMPMonitorInfoWin, GetMonitorInfoUnknownMonitor)
{
  SetMonitorInfo(MakeMonitors(), 1);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  EXPECT_FALSE(
      CachedGetMonitorInfoW(reinterpret_cast<HMONITOR>(0x9999), &info));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_MONITOR_HANDLE));
  EXPECT_FALSE(
      CachedGetMonitorInfoA(reinterpret_cast<HMONITOR>(0x9999), &info));
  EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_MONITOR_HANDLE));
}

TEST(GMPMonitorInfoWin, DisplaySettingsUniqueness)
{
  SetMonitorInfo(MakeMonitors(), 42);
  EXPECT_EQ(CachedDdQueryDisplaySettingsUniqueness(), 42u);

  SetMonitorInfo({}, 43);
  EXPECT_EQ(CachedDdQueryDisplaySettingsUniqueness(), 43u);
}

TEST(GMPMonitorInfoWin, SetMonitorInfoReplacesMonitors)
{
  SetMonitorInfo(MakeMonitors(), 1);

  nsTArray<GMPMonitorInfo> monitors = MakeMonitors();
  monitors.RemoveElementAt(0);
  SetMonitorInfo(std::move(monitors), 2);
  EXPECT_EQ(CachedDdQueryDisplaySettingsUniqueness(), 2u);

  EnumerateState state;
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&state)));
  ASSERT_EQ(state.mMonitors.Length(), 1u);
  EXPECT_EQ(state.mMonitors[0].mMonitor, kSecondary);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  EXPECT_FALSE(CachedGetMonitorInfoW(kPrimary, &info));
  EXPECT_TRUE(CachedGetMonitorInfoW(kSecondary, &info));
}

// The cached implementations should give the same results as the system APIs
// when given the monitor information collected from the system.
TEST(GMPMonitorInfoWin, MatchesSystem)
{
  SetMonitorInfo(CollectMonitorInfo(), QueryDisplaySettingsUniqueness());
  EXPECT_EQ(CachedDdQueryDisplaySettingsUniqueness(),
            QueryDisplaySettingsUniqueness());

  EnumerateState expected;
  EXPECT_TRUE(::EnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                    reinterpret_cast<LPARAM>(&expected)));

  EnumerateState actual;
  EXPECT_TRUE(CachedEnumDisplayMonitors(nullptr, nullptr, RecordMonitor,
                                        reinterpret_cast<LPARAM>(&actual)));

  ASSERT_EQ(actual.mMonitors.Length(), expected.mMonitors.Length());
  for (size_t i = 0; i < expected.mMonitors.Length(); ++i) {
    HMONITOR monitor = expected.mMonitors[i].mMonitor;
    EXPECT_EQ(actual.mMonitors[i].mMonitor, monitor);
    ExpectSameRect(actual.mMonitors[i].mRect, expected.mMonitors[i].mRect);

    MONITORINFOEXW expectedInfo{};
    expectedInfo.cbSize = sizeof(expectedInfo);
    ASSERT_TRUE(::GetMonitorInfoW(monitor, &expectedInfo));

    MONITORINFOEXW actualInfo{};
    actualInfo.cbSize = sizeof(actualInfo);
    ASSERT_TRUE(CachedGetMonitorInfoW(monitor, &actualInfo));

    ExpectSameRect(actualInfo.rcMonitor, expectedInfo.rcMonitor);
    ExpectSameRect(actualInfo.rcWork, expectedInfo.rcWork);
    EXPECT_EQ(actualInfo.dwFlags, expectedInfo.dwFlags);
    EXPECT_STREQ(actualInfo.szDevice, expectedInfo.szDevice);
  }
}
