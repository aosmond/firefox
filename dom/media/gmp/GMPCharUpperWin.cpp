/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPCharUpperWin.h"

#include <windows.h>

#include "nsWindowsDllInterceptor.h"

namespace mozilla::gmp {

static WCHAR ToUpperInvariant(WCHAR aChar) {
  WCHAR result;
  if (::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, &aChar, 1,
                      &result, 1, nullptr, nullptr, 0) != 1) {
    return aChar;
  }
  return result;
}

LPWSTR WINAPI LocaleInvariantCharUpperW(LPWSTR aString) {
  // As with CharUpperW, if the high-order bits are zero, then the low-order
  // word is a single character to convert rather than a string.
  if (IS_INTRESOURCE(aString)) {
    WCHAR result =
        ToUpperInvariant(LOWORD(reinterpret_cast<uintptr_t>(aString)));
    return reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(result));
  }

  for (LPWSTR p = aString; *p; ++p) {
    *p = ToUpperInvariant(*p);
  }
  return aString;
}

MOZ_RUNINIT static WindowsDllInterceptor sCharUpperUser32Intercept;

static WindowsDllInterceptor::FuncHookType<decltype(&::CharUpperW)>
    sOriginalCharUpperW;

void InitializeCharUpperHook() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  sCharUpperUser32Intercept.Init("user32.dll");
  if (!sOriginalCharUpperW.Set(sCharUpperUser32Intercept, "CharUpperW",
                               &LocaleInvariantCharUpperW)) {
    NS_WARNING("Failed to hook CharUpperW");
  }
}

}  // namespace mozilla::gmp
