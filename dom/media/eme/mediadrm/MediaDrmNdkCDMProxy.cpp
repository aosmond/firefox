/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmNdkCDMProxy.h"

#include "media/NdkMediaDrm.h"
#include <dlfcn.h>

namespace mozilla {

StaticAutoPtr<MediaDrmNdkCDMProxy::Internals> MediaDrmNdkCDMProxy::sMediaNdk;
std::map<AMediaDrm*, MediaDrmNdkCDMProxy*> MediaDrmNdkCDMProxy::sMediaDrmCbMap;

MediaDrmNdkCDMProxy::Internals::~Internals() {
  if (mLib) {
    dlclose(mLib);
  }
}

/* static */
bool MediaDrmNdkCDMProxy::InitializeStatics() {
  if (sMediaNdk) {
    return true;
  }

  void* lib = dlopen("libmediandk.so", RTLD_NOW);
  if (!lib) {
    return false;
  }

  auto mediaNdk = MakeUnique<MediaDrmNdkCDMProxy::Internals>(lib);

#define LOAD_OPTIONAL_SYMBOL(objName, fnName) \
  mediaNdk->m##objName##_##fnName =           \
      (objName##FnPtr_##fnName)dlsym(lib, "" #fnName);

#define LOAD_REQUIRED_SYMBOL(objName, fnName)         \
  LOAD_OPTIONAL_SYMBOL(objName, fnName);              \
  if (NS_WARN_IF(!mediaNdk->m##objName##_##fnName)) { \
    return false;                                     \
  }

  LOAD_REQUIRED_SYMBOL(AMediaDrm, createByUUID);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, openSession);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, closeSession);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, getProvisionRequest);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, getKeyRequest);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, provideProvisionResponse);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, provideKeyResponse);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, setOnEventListener);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, setPropertyByteArray);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, setPropertyString);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, release);
  LOAD_REQUIRED_SYMBOL(AMediaDrm, setOnEventListener);
  LOAD_OPTIONAL_SYMBOL(AMediaDrm, setOnExpirationUpdateListener);
  LOAD_OPTIONAL_SYMBOL(AMediaDrm, setOnKeysChangeListener);

  LOAD_REQUIRED_SYMBOL(AMediaCrypto, isCryptoSchemeSupported);
  LOAD_REQUIRED_SYMBOL(AMediaCrypto, delete);
  LOAD_REQUIRED_SYMBOL(AMediaCrypto, new);

#undef LOAD_REQUIRED_SYMBOL
#undef LOAD_OPTIONAL_SYMBOL

  sMediaNdk = mediaNdk.release();
  return true;
}

/* static */
void MediaDrmNdkCDMProxy::HandleEventCb(AMediaDrm* aDrm,
                                        const AMediaDrmSessionId* aSessionId,
                                        AMediaDrmEventType aEventType,
                                        int aExtra, const uint8_t* aData,
                                        size_t aDataSize) {
  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  UniquePtr<uint8_t[]> sessionId;
  size_t sessionIdLength =
      aSessionId && aSessionId->ptr ? aSessionId->length : 0;
  if (sessionIdLength > 0) {
    sessionId.reset(new uint8_t[sessionIdLength]);
    memcpy(sessionId.get(), aSessionId->ptr, sessionIdLength);
  }

  UniquePtr<uint8_t[]> data;
  size_t dataSize = aData ? aDataSize : 0;
  if (dataSize) {
    data.reset(new uint8_t[dataSize]);
    memcpy(data.get(), aData, dataSize);
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__,
      [aDrm, aEventType, aExtra, dataSize, sessionIdLength,
       sessionIdData = std::move(sessionId), data = std::move(data)]() mutable {
        AMediaDrmSessionId sessionId{};
        sessionId.length = sessionIdLength;
        sessionId.ptr = sessionIdData.get();

        auto i = sMediaDrmCbMap.find(aDrm);
        if (i == sMediaDrmCbMap.end()) {
          return;
        }

        i->second->HandleEvent(&sessionId, aEventType, aExtra, std::move(data),
                               dataSize);
      }));
}

/* static */
void MediaDrmNdkCDMProxy::HandleExpirationUpdateCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    int64_t aExpiryTimeInMS) {}

/* static */
void MediaDrmNdkCDMProxy::HandleKeysChangeCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    const AMediaDrmKeyStatus* aKeyStatus, size_t aNumKeys,
    bool aHasNewUsableKey) {}

MediaDrmNdkCDMProxy::MediaDrmNdkCDMProxy(dom::MediaKeys* aKeys,
                                         const nsAString& aKeySystem,
                                         bool aDistinctiveIdentifierRequired,
                                         bool aPersistentStateRequired)
    : CDMProxy(aKeys, aKeySystem, aDistinctiveIdentifierRequired,
               aPersistentStateRequired) {}

MediaDrmNdkCDMProxy::~MediaDrmNdkCDMProxy() { Destroy(); }

void MediaDrmNdkCDMProxy::Destroy() {
  if (mCrypto) {
    sMediaNdk->mAMediaCrypto_delete(mCrypto);
    mCrypto = nullptr;
  }

  if (mDrm) {
    sMediaDrmCbMap.erase(mDrm);
    sMediaNdk->mAMediaDrm_release(mDrm);
    mDrm = nullptr;
  }

  mCryptoSessionId = {};
}

void MediaDrmNdkCDMProxy::HandleEvent(const AMediaDrmSessionId* aSessionId,
                                      AMediaDrmEventType aEventType, int aExtra,
                                      UniquePtr<uint8_t[]>&& aData,
                                      size_t aDataSize) {}

void MediaDrmNdkCDMProxy::RejectPromise(PromiseId aPromiseId,
                                        MediaResult&& aResult) {
  ErrorResult rv;
  aResult.ThrowTo(rv);
  RejectPromise(aPromiseId, std::move(rv), aResult.Message());
}

void MediaDrmNdkCDMProxy::RejectPromise(dom::PromiseId aPromiseId,
                                        media_status_t aStatus,
                                        const nsCString& aMessage) {
  ErrorResult rv;
  rv.ThrowInvalidStateError(aMessage);
  RejectPromise(aPromiseId, std::move(rv), aMessage);
}

media_status_t MediaDrmNdkCDMProxy::RequestProvision() {
  if (mProvisionRequestOutstanding) {
    return AMEDIA_OK;
  }

  if (!mDrm) {
    return AMEDIA_ERROR_INVALID_OPERATION;
  }

  const uint8_t* provisionRequest = nullptr;
  size_t provisionRequestSize = 0;
  const char* serverUrl = nullptr;
  media_status_t status = sMediaNdk->mAMediaDrm_getProvisionRequest(
      mDrm, &provisionRequest, &provisionRequestSize, &serverUrl);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return status;
  }

  // TODO Send to content process to do fetching
  mProvisionRequestOutstanding = true;
  return AMEDIA_OK;
}

void MediaDrmNdkCDMProxy::OnProvisionResponse(const uint8_t* aResponse,
                                              size_t aResponseSize) {
  if (!mProvisionRequestOutstanding || !mDrm) {
    return;
  }

  mProvisionRequestOutstanding = false;
  media_status_t status = sMediaNdk->mAMediaDrm_provideProvisionResponse(
      mDrm, aResponse, aResponseSize);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    // TODO reject all outstanding promises
  }
}

void MediaDrmNdkCDMProxy::Init(PromiseId aPromiseId, const nsAString& aOrigin,
                               const nsAString& aTopLevelOrigin,
                               const nsAString& aGMPName) {
  if (!InitializeStatics()) {
    RejectPromise(aPromiseId, MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                                          "Cannot load media NDK symbols"_ns));
    return;
  }

  static constexpr uint8_t WIDEVINE_UUID[] = {
      0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
      0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

  if (NS_WARN_IF(
          !sMediaNdk->mAMediaCrypto_isCryptoSchemeSupported(WIDEVINE_UUID))) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "AMediaCrypto does not support UUID"_ns));
    return;
  }

  mDrm = sMediaNdk->mAMediaDrm_createByUUID(WIDEVINE_UUID);
  if (NS_WARN_IF(!mDrm)) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "Failed to create AMediaDrm with UUID"_ns));
    return;
  }

  media_status_t status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "securityLevel", "L3");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return RejectPromise(aPromiseId, status,
                         "Failed to set AMediaDrm securityLevel property"_ns);
  }

  status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "privacyMode", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return RejectPromise(aPromiseId, status,
                         "Failed to set AMediaDrm privateMode property"_ns);
  }

  status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "sessionSharing", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return RejectPromise(aPromiseId, status,
                         "Failed to set AMediaDrm sessionSharing property"_ns);
  }

  status = sMediaNdk->mAMediaDrm_setOnEventListener(mDrm, HandleEventCb);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return RejectPromise(aPromiseId, status,
                         "Failed to set AMediaDrm event listener"_ns);
  }

  if (sMediaNdk->mAMediaDrm_setOnExpirationUpdateListener) {
    status = sMediaNdk->mAMediaDrm_setOnExpirationUpdateListener(
        mDrm, HandleExpirationUpdateCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      return RejectPromise(
          aPromiseId, status,
          "Failed to set AMediaDrm expiration update listener"_ns);
    }
  }

  if (sMediaNdk->mAMediaDrm_setOnKeysChangeListener) {
    status =
        sMediaNdk->mAMediaDrm_setOnKeysChangeListener(mDrm, HandleKeysChangeCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      return RejectPromise(aPromiseId, status,
                           "Failed to set AMediaDrm keys change listener"_ns);
    }
  }

  sMediaDrmCbMap[mDrm] = this;

  status = sMediaNdk->mAMediaDrm_openSession(mDrm, &mCryptoSessionId);

  if (status == AMEDIA_DRM_NOT_PROVISIONED) {
    status = RequestProvision();
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      return RejectPromise(aPromiseId, status,
                           "Failed to request provisioning for AMediaDrm"_ns);
    }
    return;
  }

  if (NS_WARN_IF(status != AMEDIA_OK)) {
    return RejectPromise(aPromiseId, status,
                         "AMediaDrm failed to open sesssion"_ns);
  }

  mCrypto = sMediaNdk->mAMediaCrypto_new(WIDEVINE_UUID, mCryptoSessionId.ptr,
                                         mCryptoSessionId.length);
  if (NS_WARN_IF(!mCrypto)) {
    RejectPromise(aPromiseId, MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                                          "Failed to create AMediaCrypto"_ns));
    return;
  }

  ResolvePromise(aPromiseId);
}

void MediaDrmNdkCDMProxy::CreateSession(uint32_t aCreateSessionToken,
                                        MediaKeySessionType aSessionType,
                                        PromiseId aPromiseId,
                                        const nsAString& aInitDataType,
                                        nsTArray<uint8_t>& aInitData) {
  // If we are still provisioning, then the remote side should have queued the
  // requests.
  if (NS_WARN_IF(!mDrm) || NS_WARN_IF(!mCrypto)) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "Missing AMediaDrm and/or AMediaCrypto"_ns));
    return;
  }

  AMediaDrmSessionId sessionId;
  media_status_t status = sMediaNdk->mAMediaDrm_openSession(mDrm, &sessionId);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
  }
}

void MediaDrmNdkCDMProxy::LoadSession(PromiseId aPromiseId,
                                      dom::MediaKeySessionType aSessionType,
                                      const nsAString& aSessionId) {}

void MediaDrmNdkCDMProxy::SetServerCertificate(PromiseId aPromiseId,
                                               nsTArray<uint8_t>& aCert) {}

void MediaDrmNdkCDMProxy::UpdateSession(const nsAString& aSessionId,
                                        PromiseId aPromiseId,
                                        nsTArray<uint8_t>& aResponse) {}

void MediaDrmNdkCDMProxy::CloseSession(const nsAString& aSessionId,
                                       PromiseId aPromiseId) {}

void MediaDrmNdkCDMProxy::RemoveSession(const nsAString& aSessionId,
                                        PromiseId aPromiseId) {}

void MediaDrmNdkCDMProxy::QueryOutputProtectionStatus() {}

void MediaDrmNdkCDMProxy::NotifyOutputProtectionStatus(
    OutputProtectionCheckStatus aCheckStatus,
    OutputProtectionCaptureStatus aCaptureStatus) {}

void MediaDrmNdkCDMProxy::Shutdown() {}

void MediaDrmNdkCDMProxy::Terminated() {}

void MediaDrmNdkCDMProxy::OnSetSessionId(uint32_t aCreateSessionToken,
                                         const nsAString& aSessionId) {}

void MediaDrmNdkCDMProxy::OnResolveLoadSessionPromise(uint32_t aPromiseId,
                                                      bool aSuccess) {}

void MediaDrmNdkCDMProxy::OnSessionMessage(
    const nsAString& aSessionId, dom::MediaKeyMessageType aMessageType,
    const nsTArray<uint8_t>& aMessage) {}

void MediaDrmNdkCDMProxy::OnExpirationChange(const nsAString& aSessionId,
                                             UnixTime aExpiryTime) {}

void MediaDrmNdkCDMProxy::OnSessionClosed(const nsAString& aSessionId) {}

void MediaDrmNdkCDMProxy::OnSessionError(const nsAString& aSessionId,
                                         nsresult aException,
                                         uint32_t aSystemCode,
                                         const nsAString& aMsg) {}

void MediaDrmNdkCDMProxy::OnRejectPromise(uint32_t aPromiseId,
                                          ErrorResult&& aException,
                                          const nsCString& aMsg) {}

RefPtr<DecryptPromise> MediaDrmNdkCDMProxy::Decrypt(MediaRawData* aSample) {
  return nullptr;
}

void MediaDrmNdkCDMProxy::MediaDrmNdkCDMProxy::OnDecrypted(
    uint32_t aId, DecryptStatus aResult,
    const nsTArray<uint8_t>& aDecryptedData) {}

void MediaDrmNdkCDMProxy::RejectPromise(PromiseId aId, ErrorResult&& aException,
                                        const nsCString& aReason) {}

// Resolves promise with "undefined".
// Can be called from any thread.
void MediaDrmNdkCDMProxy::ResolvePromise(PromiseId aId) {}

void MediaDrmNdkCDMProxy::OnKeyStatusesChange(const nsAString& aSessionId) {}

void MediaDrmNdkCDMProxy::GetStatusForPolicy(
    PromiseId aPromiseId, const dom::HDCPVersion& aMinHdcpVersion) {}

#ifdef DEBUG
bool MediaDrmNdkCDMProxy::IsOnOwnerThread() { return false; }
#endif

}  // namespace mozilla
