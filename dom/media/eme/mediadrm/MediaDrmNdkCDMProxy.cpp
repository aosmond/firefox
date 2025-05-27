/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDrmNdkCDMProxy.h"

#include "media/NdkMediaDrm.h"
#include "mozilla/EMEUtils.h"
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
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  size_t dataSize = aData ? aDataSize : 0;
  nsTArray<uint8_t> data(dataSize);
  if (dataSize) {
    data.AppendElements(aData, dataSize);
  }

  // FIXME(aosmond) dispatch to RemoteMediaManagerParent owning thread
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__,
      [aDrm, aEventType, aExtra, sessionIdStr = std::move(sessionIdStr),
       data = std::move(data)]() mutable {
        auto i = sMediaDrmCbMap.find(aDrm);
        if (i == sMediaDrmCbMap.end()) {
          return;
        }

        i->second->HandleEvent(std::move(sessionIdStr), aEventType, aExtra,
                               std::move(data));
      }));
}

/* static */
void MediaDrmNdkCDMProxy::HandleExpirationUpdateCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    int64_t aExpiryTimeInMS) {
  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  // FIXME(aosmond) dispatch to RemoteMediaManagerParent owning thread
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [aDrm, aExpiryTimeInMS,
                 sessionIdStr = std::move(sessionIdStr)]() mutable {
        auto i = sMediaDrmCbMap.find(aDrm);
        if (i == sMediaDrmCbMap.end()) {
          return;
        }

        i->second->HandleExpirationUpdate(std::move(sessionIdStr),
                                          aExpiryTimeInMS);
      }));
}

/* static */
void MediaDrmNdkCDMProxy::HandleKeysChangeCb(
    AMediaDrm* aDrm, const AMediaDrmSessionId* aSessionId,
    const AMediaDrmKeyStatus* aKeyStatus, size_t aNumKeys,
    bool aHasNewUsableKey) {
  // Called from an internal NDK thread. We need to dispatch to the owning
  // thread of the actor with the same AMediaDrm object.
  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(aSessionId->ptr), aSessionId->length);

  nsTArray<CDMKeyInfo> keyInfo(aNumKeys);
  if (aKeyStatus) {
    for (size_t i = 0; i < aNumKeys; ++i) {
      nsTArray<uint8_t> keyId(aKeyStatus[i].keyId.ptr,
                              aKeyStatus[i].keyId.length);

      dom::Optional<dom::MediaKeyStatus> keyStatus;
      switch (aKeyStatus[i].keyType) {
        case AMediaKeyStatusType::KEY_STATUS_TYPE_USABLE:
          keyStatus.Construct(dom::MediaKeyStatus::Usable);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_EXPIRED:
          keyStatus.Construct(dom::MediaKeyStatus::Expired);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_OUTPUTNOTALLOWED:
          keyStatus.Construct(dom::MediaKeyStatus::Output_restricted);
          break;
        case AMediaKeyStatusType::KEY_STATUS_TYPE_STATUSPENDING:
          keyStatus.Construct(dom::MediaKeyStatus::Status_pending);
          break;
        default:
          MOZ_FALLTHROUGH_ASSERT("Unhandled AMediaKeyStatusType!");
        case AMediaKeyStatusType::KEY_STATUS_TYPE_INTERNALERROR:
          keyStatus.Construct(dom::MediaKeyStatus::Internal_error);
          break;
      }

      keyInfo.AppendElement(CDMKeyInfo(std::move(keyId), std::move(keyStatus)));
    }
  }

  // FIXME(aosmond) dispatch to RemoteMediaManagerParent owning thread
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [aDrm, aHasNewUsableKey, sessionIdStr = std::move(sessionIdStr),
                 keyInfo = std::move(keyInfo)]() mutable {
        auto i = sMediaDrmCbMap.find(aDrm);
        if (i == sMediaDrmCbMap.end()) {
          return;
        }

        i->second->HandleKeysChange(std::move(sessionIdStr), aHasNewUsableKey,
                                    std::move(keyInfo));
      }));
}

MediaDrmNdkCDMProxy::MediaDrmNdkCDMProxy(const nsAString& aKeySystem,
                                         bool aDistinctiveIdentifierRequired,
                                         bool aPersistentStateRequired) {
  if (IsWidevineKeySystem(aKeySystem)) {
    mUuid = WIDEVINE_UUID;
  } else if (IsClearkeyKeySystem(aKeySystem)) {
    mUuid = CLEARKEY_UUID;
  }
}

MediaDrmNdkCDMProxy::~MediaDrmNdkCDMProxy() { Destroy(); }

void MediaDrmNdkCDMProxy::Destroy() {
  for (const auto& i : mSessions) {
    sMediaNdk->mAMediaDrm_closeSession(mDrm, &i.second.id);
  }
  mSessions.clear();

  if (mCrypto) {
    sMediaNdk->mAMediaCrypto_delete(mCrypto);
    mCrypto = nullptr;
    mCryptoSessionId = {};
  }

  if (mDrm) {
    sMediaDrmCbMap.erase(mDrm);
    sMediaNdk->mAMediaDrm_release(mDrm);
    mDrm = nullptr;
  }
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvInit(
    const RemoteCDMInitRequestIPDL& request, InitResolver&& aResolver) {
  if (NS_WARN_IF(!mUuid)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid uuid"_ns));
    return IPC_OK();
  }

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

  if (NS_WARN_IF(!sMediaNdk->mAMediaCrypto_isCryptoSchemeSupported(mUuid))) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaCrypto does not support UUID"_ns));
    return IPC_OK();
  }

  mDrm = sMediaNdk->mAMediaDrm_createByUUID(mUuid);
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
  MOZ_ASSERT(mUuid);
  MOZ_ASSERT(mDrm);
  MOZ_ASSERT(!mCrypto);

  mCrypto = sMediaNdk->mAMediaCrypto_new(mUuid, mCryptoSessionId.ptr,
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
    const RemoteCDMCreateSessionRequestIPDL& aRequest,
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
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm_openSession failed"_ns));
    return IPC_OK();
  }

  NS_ConvertUTF8toUTF16 sessionIdStr(
      reinterpret_cast<const char*>(sessionId.ptr), sessionId.length);
  mSessions[sessionIdStr] = {sessionId};

  aResolver(std::move(sessionIdStr));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvLoadSession(
    const RemoteCDMLoadSessionRequestIPDL& aRequest,
    LoadSessionResolver&& aResolver) {
  aResolver(MediaResult(NS_ERROR_NOT_IMPLEMENTED));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvUpdateSession(
    const RemoteCDMUpdateSessionRequestIPDL& aRequest,
    UpdateSessionResolver&& aResolver) {
  if (NS_WARN_IF(!mDrm) || NS_WARN_IF(!mCrypto)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Missing AMediaDrm and/or AMediaCrypto"_ns));
    return IPC_OK();
  }

  const auto i = mSessions.find(aRequest.sessionId());
  if (i == mSessions.end()) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid session id"_ns));
    return IPC_OK();
  }

  AMediaDrmKeySetId keySetId{};
  media_status_t status = sMediaNdk->mAMediaDrm_provideKeyResponse(
      mDrm, &i->second.id, aRequest.response().Elements(),
      aRequest.response().Length(), &keySetId);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm_provideKeyResponse failed"_ns));
    return IPC_OK();
  }

  aResolver(MediaResult(NS_OK));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvRemoveSession(
    const nsString& aSessionId, RemoveSessionResolver&& aResolver) {
  aResolver(MediaResult(NS_ERROR_NOT_IMPLEMENTED));
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvCloseSession(
    const nsString& aSessionId, CloseSessionResolver&& aResolver) {
  if (NS_WARN_IF(!mDrm) || NS_WARN_IF(!mCrypto)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Missing AMediaDrm and/or AMediaCrypto"_ns));
    return IPC_OK();
  }

  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    aResolver(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR, "Invalid session id"_ns));
    return IPC_OK();
  }

  media_status_t status =
      sMediaNdk->mAMediaDrm_closeSession(mDrm, &i->second.id);
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "AMediaDrm_closeSession failed"_ns));
  } else {
    aResolver(MediaResult(NS_OK));
  }

  mSessions.erase(i);
  return IPC_OK();
}

mozilla::ipc::IPCResult MediaDrmNdkCDMProxy::RecvSetServerCertificate(
    mozilla::Span<uint8_t const> aCertificate,
    SetServerCertificateResolver&& aResolver) {
  if (NS_WARN_IF(!mDrm) || NS_WARN_IF(!mCrypto)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Missing AMediaDrm and/or AMediaCrypto"_ns));
    return IPC_OK();
  }

  media_status_t status = sMediaNdk->mAMediaDrm_setPropertyByteArray(
      mDrm, "certificate", aCertificate.Elements(), aCertificate.Length());
  if (NS_WARN_IF(status != AMEDIA_OK)) {
    aResolver(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "Failed to set AMediaDrm certificate property"_ns));
    return IPC_OK();
  }
  return IPC_OK();
}

void MediaDrmNdkCDMProxy::HandleEvent(nsString&& aSessionId,
                                      AMediaDrmEventType aEventType, int aExtra,
                                      nsTArray<uint8_t>&& aData) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    return;
  }

  switch (aEventType) {
    case AMediaDrmEventType::EVENT_PROVISION_REQUIRED:
      break;
    case AMediaDrmEventType::EVENT_KEY_REQUIRED:
      break;
    case AMediaDrmEventType::EVENT_KEY_EXPIRED:
      break;
    case AMediaDrmEventType::EVENT_VENDOR_DEFINED:
      break;
    case AMediaDrmEventType::EVENT_SESSION_RECLAIMED:
      break;
    default:
      break;
  }
}

void MediaDrmNdkCDMProxy::HandleExpirationUpdate(nsString&& aSessionId,
                                                 int aExpiryTimeInMS) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    return;
  }

  Unused << SendOnSessionKeyExpiration(
      RemoteCDMKeyExpirationIPDL(std::move(aSessionId), aExpiryTimeInMS));
}

void MediaDrmNdkCDMProxy::HandleKeysChange(nsString&& aSessionId,
                                           bool aHasNewUsableKey,
                                           nsTArray<CDMKeyInfo>&& aKeyInfo) {
  const auto i = mSessions.find(aSessionId);
  if (i == mSessions.end()) {
    return;
  }

  Unused << SendOnSessionKeyStatus(
      RemoteCDMKeyStatusIPDL(std::move(aSessionId), std::move(aKeyInfo)));
}

}  // namespace mozilla
