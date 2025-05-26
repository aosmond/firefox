/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmNdkCDMProxy_h_
#define MediaDrmNdkCDMProxy_h_

#include "mozilla/RemoteCDMParent.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"

#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaDrm.h"
#include "media/NdkMediaError.h"

#include <map>

namespace mozilla {

// Requires API level 21+.
using AMediaCryptoFnPtr_isCryptoSchemeSupported = bool (*)(const AMediaUUID);
using AMediaCryptoFnPtr_delete = void (*)(AMediaCrypto*);
using AMediaCryptoFnPtr_new = AMediaCrypto* (*)(const AMediaUUID, const void*,
                                                size_t);

using AMediaDrmFnPtr_createByUUID = AMediaDrm* (*)(const uint8_t*);
using AMediaDrmFnPtr_openSession = media_status_t (*)(AMediaDrm*,
                                                      AMediaDrmSessionId*);
using AMediaDrmFnPtr_closeSession =
    media_status_t (*)(AMediaDrm*, const AMediaDrmSessionId*);
using AMediaDrmFnPtr_getProvisionRequest = media_status_t (*)(AMediaDrm*,
                                                              const uint8_t**,
                                                              size_t*,
                                                              const char**);
using AMediaDrmFnPtr_getKeyRequest = media_status_t (*)(
    AMediaDrm*, const AMediaDrmScope*, const uint8_t*, size_t, const char*,
    AMediaDrmKeyType, const AMediaDrmKeyValue*, size_t, const uint8_t**,
    size_t*);
using AMediaDrmFnPtr_provideProvisionResponse =
    media_status_t (*)(AMediaDrm*, const uint8_t*, size_t);
using AMediaDrmFnPtr_provideKeyResponse =
    media_status_t (*)(AMediaDrm*, const AMediaDrmScope*, const uint8_t*,
                       size_t, AMediaDrmKeySetId*);
using AMediaDrmFnPtr_setOnEventListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmEventListener);
using AMediaDrmFnPtr_setPropertyByteArray = media_status_t (*)(AMediaDrm*,
                                                               const char*,
                                                               const uint8_t*,
                                                               size_t);
using AMediaDrmFnPtr_setPropertyString = media_status_t (*)(AMediaDrm*,
                                                            const char*,
                                                            const char*);
using AMediaDrmFnPtr_release = void (*)(AMediaDrm*);

// Requires API level 29+, but can be built with NDKs supported API level 21+.
using AMediaDrmFnPtr_ExpirationUpdateListener =
    void (*)(AMediaDrm*, const AMediaDrmSessionId*, int64_t);
using AMediaDrmFnPtr_KeysChangeListener = void (*)(AMediaDrm*,
                                                   const AMediaDrmSessionId*,
                                                   const AMediaDrmKeyStatus*,
                                                   size_t, bool);
using AMediaDrmFnPtr_setOnExpirationUpdateListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmFnPtr_ExpirationUpdateListener);
using AMediaDrmFnPtr_setOnKeysChangeListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmFnPtr_KeysChangeListener);

class MediaDrmNdkCDMProxy final : public RemoteCDMParent {
 public:
  MediaDrmNdkCDMProxy(const nsAString& aKeySystem,
                      bool aDistinctiveIdentifierRequired,
                      bool aPersistentStateRequired);

  // PRemoteCDMParent
  mozilla::ipc::IPCResult RecvInit(const RemoteCDMInitRequestIPDL& request,
                                   InitResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCreateSession(
      const RemoteCDMCreateSessionRequestIPDL& request,
      CreateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvLoadSession(
      const RemoteCDMLoadSessionRequestIPDL& request,
      LoadSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvUpdateSession(
      const RemoteCDMUpdateSessionRequestIPDL& request,
      UpdateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvRemoveSession(
      const nsAString& sessionId, RemoveSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCloseSession(
      const nsAString& sessionId, CloseSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvSetServerCertificate(
      mozilla::Span<uint8_t const> certificate,
      SetServerCertificateResolver&& aResolver) override;

 private:
  virtual ~MediaDrmNdkCDMProxy();

  static bool InitializeStatics();

  static void HandleEventCb(AMediaDrm* aMediaDrm,
                            const AMediaDrmSessionId* aSessionId,
                            AMediaDrmEventType aEventType, int aExtra,
                            const uint8_t* aData, size_t aDataSize);

  static void HandleExpirationUpdateCb(AMediaDrm* aDrm,
                                       const AMediaDrmSessionId* aSessionId,
                                       int64_t aExpiryTimeInMS);

  static void HandleKeysChangeCb(AMediaDrm* aDrm,
                                 const AMediaDrmSessionId* aSessionId,
                                 const AMediaDrmKeyStatus* aKeyStatus,
                                 size_t aNumKeys, bool aHasNewUsableKey);

  void HandleEvent(const AMediaDrmSessionId* aSessionId,
                   AMediaDrmEventType aEventType, int aExtra,
                   UniquePtr<uint8_t[]>&& aData, size_t aDataSize);

  void OnProvisionResponse(const uint8_t* aResponse, size_t aResponseSize);
  media_status_t RequestProvision();

  void Destroy();

  struct Internals {
    explicit Internals(void* aLib) : mLib(aLib) {}
    ~Internals();

    void* mLib = nullptr;

#define DEFINE_AMEDIA_SYMBOL(objName, fnName) \
  objName##FnPtr_##fnName m##objName##_##fnName = nullptr;
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, createByUUID);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, openSession);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, closeSession);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, getProvisionRequest);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, getKeyRequest);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, provideProvisionResponse);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, provideKeyResponse);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnEventListener);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setPropertyByteArray);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setPropertyString);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, release);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnExpirationUpdateListener);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnKeysChangeListener);

    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, isCryptoSchemeSupported);
    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, delete);
    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, new);
#undef DEFINE_AMEDIA_SYMBOL
  };

  static StaticAutoPtr<Internals> sMediaNdk;

  static std::map<AMediaDrm*, MediaDrmNdkCDMProxy*> sMediaDrmCbMap;

  struct SessionEntry {};

  std::map<nsCString, SessionEntry> mSessions;

  AMediaDrm* mDrm = nullptr;
  AMediaCrypto* mCrypto = nullptr;
  AMediaDrmSessionId mCryptoSessionId;
  bool mProvisionRequestOutstanding = false;
};

}  // namespace mozilla

#endif  // MediaDrmNdkCDMProxy_h_
