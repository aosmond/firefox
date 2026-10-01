/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include <string>

#include "GMPCharUpperWin.h"
#include "gtest/gtest.h"

using namespace mozilla::gmp;

static WCHAR UpperChar(LPWSTR(WINAPI* aCharUpper)(LPWSTR), WCHAR aChar) {
  return LOWORD(reinterpret_cast<uintptr_t>(
      aCharUpper(reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(aChar)))));
}

static WCHAR SystemUpperChar(WCHAR aChar) {
  return UpperChar(::CharUpperW, aChar);
}

static WCHAR InvariantUpperChar(WCHAR aChar) {
  return UpperChar(LocaleInvariantCharUpperW, aChar);
}

TEST(GMPCharUpperWin, SingleCharacter)
{
  EXPECT_EQ(InvariantUpperChar(L'a'), L'A');
  EXPECT_EQ(InvariantUpperChar(L'z'), L'Z');
  EXPECT_EQ(InvariantUpperChar(L'A'), L'A');
  EXPECT_EQ(InvariantUpperChar(L'1'), L'1');
  EXPECT_EQ(InvariantUpperChar(L'\\'), L'\\');
  EXPECT_EQ(InvariantUpperChar(0x00E9), WCHAR(0x00C9));  // e with acute accent
  EXPECT_EQ(InvariantUpperChar(0x03B1), WCHAR(0x0391));  // Greek alpha
  EXPECT_EQ(InvariantUpperChar(0), WCHAR(0));
}

TEST(GMPCharUpperWin, String)
{
  wchar_t buffer[] = L"c:\\program files\\Mozilla\\widevinecdm.dll";
  EXPECT_EQ(LocaleInvariantCharUpperW(buffer), buffer);
  EXPECT_STREQ(buffer, L"C:\\PROGRAM FILES\\MOZILLA\\WIDEVINECDM.DLL");
}

TEST(GMPCharUpperWin, EmptyString)
{
  wchar_t buffer[] = L"";
  EXPECT_EQ(LocaleInvariantCharUpperW(buffer), buffer);
  EXPECT_STREQ(buffer, L"");
}

TEST(GMPCharUpperWin, StringMatchesSystem)
{
  std::wstring input = L"abcXYZ019 \u00e9\u00ff\u00df\u0131\u03c3\u0451\uff41";
  std::wstring expected = input;
  std::wstring actual = input;
  ::CharUpperW(expected.data());
  LocaleInvariantCharUpperW(actual.data());
  EXPECT_EQ(actual, expected);
}

// The single character form should match the system implementation for every
// UTF-16 code unit, since it is used to compare strings case insensitively.
TEST(GMPCharUpperWin, SingleCharacterMatchesSystem)
{
  size_t mismatches = 0;
  for (uint32_t c = 0; c <= 0xFFFF; ++c) {
    WCHAR expected = SystemUpperChar(static_cast<WCHAR>(c));
    WCHAR actual = InvariantUpperChar(static_cast<WCHAR>(c));
    if (actual != expected && ++mismatches <= 16) {
      ADD_FAILURE() << "U+" << std::hex << c << ": expected U+"
                    << uint32_t(expected) << " but got U+" << uint32_t(actual);
    }
  }
  EXPECT_EQ(mismatches, 0u);
}
