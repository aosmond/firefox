/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMacUtilsImpl_h_
#define nsMacUtilsImpl_h_

#include "mozilla/Atomics.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsString.h"

using mozilla::Atomic;
using mozilla::StaticAutoPtr;
using mozilla::StaticMutex;

class nsIFile;

namespace nsMacUtilsImpl {

// Return the repo directory and the repo object directory respectively.
// These should only be used on Mac developer builds to determine the path
// to the repo or object directory.
nsresult GetRepoDir(nsIFile** aRepoDir);
nsresult GetObjDir(nsIFile** aObjDir);

#if defined(MOZ_SANDBOX)
bool GetAppPath(nsCString& aAppPath);
#endif /* MOZ_SANDBOX */

#if defined(MOZ_SANDBOX) && defined(DEBUG)
nsresult GetBloatLogDir(nsCString& aDirectoryPath);
nsresult GetDirectoryPath(const char* aPath, nsCString& aDirectoryPath);
#endif /* MOZ_SANDBOX && DEBUG */

void EnableTCSMIfAvailable();
bool IsTCSMAvailable();
uint32_t GetPhysicalCPUCount();
nsresult GetArchitecturesForBinary(const char* aPath, uint32_t* aArchMask);

enum class CodeSignatureType {
  UnexpectedError,
  Unsigned,
  AdHoc,
  DeveloperID,
  AppStore,
  AppleSystem,
  Development,
  Other
};

// Returns a string representation of the provided code signature type enum.
// On failure, returns CodeSignatureType::UnexpectedError.
nsCString CodeSignatureTypeToString(CodeSignatureType aType);

// Returns the signature type for the binary at `aPath`.
CodeSignatureType GetSignatureType(const nsCString& aPath);

}  // namespace nsMacUtilsImpl

#endif /* nsMacUtilsImpl_h_ */
