/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmNdkCDMProxy_h_
#define MediaDrmNdkCDMProxy_h_

#include "mozilla/CDMProxy.h"
#include "mozilla/CDMCaps.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/dom/MediaKeySession.h"
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

class MediaDrmNdkCDMProxy final : public CDMProxy {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmNdkCDMProxy, override)

  MediaDrmNdkCDMProxy(dom::MediaKeys* aKeys, const nsAString& aKeySystem,
                      bool aDistinctiveIdentifierRequired,
                      bool aPersistentStateRequired);

  void Init(PromiseId aPromiseId, const nsAString& aOrigin,
            const nsAString& aTopLevelOrigin,
            const nsAString& aGMPName) override;

  void CreateSession(uint32_t aCreateSessionToken,
                     MediaKeySessionType aSessionType, PromiseId aPromiseId,
                     const nsAString& aInitDataType,
                     nsTArray<uint8_t>& aInitData) override;

  void LoadSession(PromiseId aPromiseId, dom::MediaKeySessionType aSessionType,
                   const nsAString& aSessionId) override;

  void SetServerCertificate(PromiseId aPromiseId,
                            nsTArray<uint8_t>& aCert) override;

  void UpdateSession(const nsAString& aSessionId, PromiseId aPromiseId,
                     nsTArray<uint8_t>& aResponse) override;

  void CloseSession(const nsAString& aSessionId, PromiseId aPromiseId) override;

  void RemoveSession(const nsAString& aSessionId,
                     PromiseId aPromiseId) override;

  void QueryOutputProtectionStatus() override;

  void NotifyOutputProtectionStatus(
      OutputProtectionCheckStatus aCheckStatus,
      OutputProtectionCaptureStatus aCaptureStatus) override;

  void Shutdown() override;

  void Terminated() override;

  void OnSetSessionId(uint32_t aCreateSessionToken,
                      const nsAString& aSessionId) override;

  void OnResolveLoadSessionPromise(uint32_t aPromiseId, bool aSuccess) override;

  void OnSessionMessage(const nsAString& aSessionId,
                        dom::MediaKeyMessageType aMessageType,
                        const nsTArray<uint8_t>& aMessage) override;

  void OnExpirationChange(const nsAString& aSessionId,
                          UnixTime aExpiryTime) override;

  void OnSessionClosed(const nsAString& aSessionId) override;

  void OnSessionError(const nsAString& aSessionId, nsresult aException,
                      uint32_t aSystemCode, const nsAString& aMsg) override;

  void OnRejectPromise(uint32_t aPromiseId, ErrorResult&& aException,
                       const nsCString& aMsg) override;

  RefPtr<DecryptPromise> Decrypt(MediaRawData* aSample) override;
  void OnDecrypted(uint32_t aId, DecryptStatus aResult,
                   const nsTArray<uint8_t>& aDecryptedData) override;

  void RejectPromise(PromiseId aId, MediaResult&& aResult);

  void RejectPromise(PromiseId aId, ErrorResult&& aException,
                     const nsCString& aReason) override;

  void RejectPromise(dom::PromiseId aPromiseId, media_status_t aStatus,
                     const nsCString& aMessage);

  // Resolves promise with "undefined".
  // Can be called from any thread.
  void ResolvePromise(PromiseId aId) override;

  void OnKeyStatusesChange(const nsAString& aSessionId) override;

  void GetStatusForPolicy(PromiseId aPromiseId,
                          const dom::HDCPVersion& aMinHdcpVersion) override;

#ifdef DEBUG
  bool IsOnOwnerThread() override;
#endif

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
