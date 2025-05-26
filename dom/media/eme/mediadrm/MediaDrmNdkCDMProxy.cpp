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

MediaDrmNdkCDMProxy::MediaDrmNdkCDMProxy(const nsAString& aKeySystem,
                                         bool aDistinctiveIdentifierRequired,
                                         bool aPersistentStateRequired) {}

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

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvInit(
    const RemoteCDMInitRequestIPDL& request, InitResolver&& aResolver) {
  if (NS_WARN_IF(mDrm) || NS_WARN_IF(mCrypto)) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Already initialized"_ns));
    return IPC_OK();
  }

  if (!InitializeStatics()) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Cannot load media NDK symbols"_ns));
    return IPC_OK();
  }

  static constexpr uint8_t WIDEVINE_UUID[] = {
      0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
      0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

  if (NS_WARN_IF(
          !sMediaNdk->mAMediaCrypto_isCryptoSchemeSupported(WIDEVINE_UUID))) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaCrypto does not support UUID"_ns));
    return IPC_OK();
  }

  mDrm = sMediaNdk->mAMediaDrm_createByUUID(WIDEVINE_UUID);
  if (NS_WARN_IF(!mDrm)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to create AMediaDrm with UUID"_ns));
    return IPC_OK();
  }

  media_status_t status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "securityLevel", "L3");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to set AMediaDrm securityLevel property"_ns));
    return IPC_OK();
  }

  status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "privacyMode", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to set AMediaDrm privateMode property"_ns));
    return IPC_OK();
  }

  status =
      sMediaNdk->mAMediaDrm_setPropertyString(mDrm, "sessionSharing", "enable");
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                    "Failed to set AMediaDrm sessionSharing property"_ns));
    return IPC_OK();
  }

  status = sMediaNdk->mAMediaDrm_setOnEventListener(mDrm, HandleEventCb);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to set AMediaDrm event listener"_ns));
    return IPC_OK();
  }

  if (sMediaNdk->mAMediaDrm_setOnExpirationUpdateListener) {
    status = sMediaNdk->mAMediaDrm_setOnExpirationUpdateListener(
        mDrm, HandleExpirationUpdateCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      aResolver(
          MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                      "Failed to set AMediaDrm expiration update listener"_ns));
      return IPC_OK();
    }
  }

  if (sMediaNdk->mAMediaDrm_setOnKeysChangeListener) {
    status =
        sMediaNdk->mAMediaDrm_setOnKeysChangeListener(mDrm, HandleKeysChangeCb);
    if (NS_WARN_IF(status != AMEDIA_OK)) {
      aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                            "Failed to set AMediaDrm keys change listener"_ns));
      return IPC_OK();
    }
  }

  sMediaDrmCbMap[mDrm] = this;

  status = sMediaNdk->mAMediaDrm_openSession(mDrm, &mCryptoSessionId);

  if (status == AMEDIA_DRM_NOT_PROVISIONED) {
    return RequestProvision(std::move(aResolver));
  }

  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm failed to open sesssion"_ns));
    return IPC_OK();
  }

  return FinishInit(std::move(aResolver));
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RequestProvision(
    InitResolver&& aResolver) {
  MOZ_ASSERT(mDrm);
  MOZ_ASSERT(!mCrypto);

  const uint8_t* provisionRequest = nullptr;
  size_t provisionRequestSize = 0;
  const char* serverUrl = nullptr;
  media_status_t status = sMediaNdk->mAMediaDrm_getProvisionRequest(
      mDrm, &provisionRequest, &provisionRequestSize, &serverUrl);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to request provisioning for AMediaDrm"_ns));
    return IPC_OK();
  }

  SendProvision(RemoteCDMProvisionRequestIPDL(
                    nsCString(serverUrl),
                    nsTArray<uint8_t>(provisionRequest, provisionRequestSize)))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, resolver = std::move(aResolver)](
              RemoteCDMProvisionResponseIPDL&& aResponse) mutable {
            if (aResponse.type() ==
                RemoteCDMProvisionResponseIPDL::TMediaResult) {
              resolver(MediaResult(
                  NS_ERROR_DOM_INVALID_STATE_ERR,
                  "Content failed to get provisioning response"_ns));
              return;
            }

            media_status_t status =
                sMediaNdk->mAMediaDrm_provideProvisionResponse(
                    self->mDrm, aResponse.get_ArrayOfuint8_t().Elements(),
                    aResponse.get_ArrayOfuint8_t().Length());
            if (NS_WARN_IF(status != AMEDIA_OK)) {
              resolver(MediaResult(
                  NS_ERROR_DOM_INVALID_STATE_ERR,
                  "AMediaDrm failed to accept provision response"_ns));
              return;
            }

            self->FinishInit(std::move(resolver));
          },
          [](const mozilla::ipc::ResponseRejectReason& aReason) {});
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::FinishInit(
    InitResolver&& aResolver) {
  MOZ_ASSERT(mDrm);
  MOZ_ASSERT(!mCrypto);

  static constexpr uint8_t WIDEVINE_UUID[] = {
      0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
      0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

  mCrypto = sMediaNdk->mAMediaCrypto_new(WIDEVINE_UUID, mCryptoSessionId.ptr,
                                         mCryptoSessionId.length);
  if (NS_WARN_IF(!mCrypto)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to create AMediaCrypto"_ns));
    return IPC_OK();
  }

  aResolver(MediaResult(NS_OK));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvCreateSession(
    const RemoteCDMCreateSessionRequestIPDL& request,
    CreateSessionResolver&& aResolver) {
  // If we are still provisioning, then the remote side should have queued the
  // requests.
  if (NS_WARN_IF(!mDrm) || NS_WARN_IF(!mCrypto)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Missing AMediaDrm and/or AMediaCrypto"_ns));
    return IPC_OK();
  }

  AMediaDrmSessionId sessionId;
  media_status_t status = sMediaNdk->mAMediaDrm_openSession(mDrm, &sessionId);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvLoadSession(
    const RemoteCDMLoadSessionRequestIPDL& request,
    LoadSessionResolver&& aResolver) {
  aResolver(MediaResult(NS_ERROR_NOT_IMPLEMENTED));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvUpdateSession(
    const RemoteCDMUpdateSessionRequestIPDL& request,
    UpdateSessionResolver&& aResolver) {
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvRemoveSession(
    const nsAString& sessionId, RemoveSessionResolver&& aResolver) {
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvCloseSession(
    const nsAString& sessionId, CloseSessionResolver&& aResolver) {
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvSetServerCertificate(
    mozilla::Span<uint8_t const> certificate,
    SetServerCertificateResolver&& aResolver) {
  return IPC_OK();
}

void MediaDrmNdkCDMProxy::HandleEvent(const AMediaDrmSessionId* aSessionId,
                                      AMediaDrmEventType aEventType, int aExtra,
                                      UniquePtr<uint8_t[]>&& aData,
                                      size_t aDataSize) {}

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

}  // namespace mozilla
