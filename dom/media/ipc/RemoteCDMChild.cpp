/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteCDMChild.h"
#include "mozilla/dom/MediaKeySession.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/dom/PromiseNativeHandler.h"
#  include "nsComponentManagerUtils.h"
#  include "nsIMediaDrmProvisioning.h"
#endif

namespace mozilla {

#  ifdef MOZ_WIDGET_ANDROID
class RemoteCDMProvisionHelper final : public dom::PromiseNativeHandler {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit RemoteCDMProvisionHelper(
      PRemoteCDMChild::ProvisionResolver&& aResolver)
      : mEventTarget(GetCurrentSerialEventTarget()),
        mResolver(std::move(aResolver)) {}

  template <class T>
  void MaybeResolve(T&& aResult) {
    if (!mResolver) {
      return;
    }

    Unused << mEventTarget->Dispatch(NS_NewRunnableFunction(
        __func__, [result = std::move(aResult),
                   resolver = std::move(mResolver)]() { resolver(result); }));
    mResolver = nullptr;
  }

  MOZ_CAN_RUN_SCRIPT
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mResolver);

    dom::ArrayBufferView view;
    if (!view.Init(aValue.toObjectOrNull())) {
      MaybeResolve(MediaResult(
          NS_ERROR_DOM_INVALID_STATE_ERR,
          "Failed to initialize ArrayBufferView for provisioning response"_ns));
      return;
    }

    nsTArray<uint8_t> response;
    if (!view.AppendDataTo(response)) {
      MaybeResolve(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                               "Failed to copy provision response"_ns));
      return;
    }

    MaybeResolve(std::move(response));
  }

  MOZ_CAN_RUN_SCRIPT
  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mResolver);
    MaybeResolve(MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                             "Failed to fetch provisioning response"_ns));
  }

 private:
  ~RemoteCDMProvisionHelper() override {
    MaybeResolve(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                    "Failed to resolve or reject provisioning promise"_ns));
  }

  nsCOMPtr<nsISerialEventTarget> mEventTarget;
  PRemoteCDMChild::ProvisionResolver mResolver;
};

NS_IMPL_ISUPPORTS0(RemoteCDMProvisionHelper);
#  endif  // MOZ_WIDGET_ANDROID

RemoteCDMChild::RemoteCDMChild(nsCOMPtr<nsISerialEventTarget>&& aThread,
                               RemoteMediaIn aLocation, dom::MediaKeys* aKeys,
                               const nsAString& aKeySystem,
                               bool aDistinctiveIdentifierRequired,
                               bool aPersistentStateRequired)
    : CDMProxy(aKeys, aKeySystem, aDistinctiveIdentifierRequired,
               aPersistentStateRequired),
      mThread(std::move(aThread)),
      mLocation(aLocation) {}

RemoteCDMChild::~RemoteCDMChild() = default;

RemoteMediaManagerChild* RemoteCDMChild::GetManager() { return nullptr; }

void RemoteCDMChild::MaybeDestroyActor() {}

void RemoteCDMChild::ActorDestroy(ActorDestroyReason aWhy) {}

mozilla::ipc::IPCResult RemoteCDMChild::RecvProvision(
    RemoteCDMProvisionRequestIPDL&& aRequest, ProvisionResolver&& aResolver) {
#  ifdef MOZ_WIDGET_ANDROID
  auto helper = MakeRefPtr<RemoteCDMProvisionHelper>(std::move(aResolver));
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [request = std::move(aRequest), helper = std::move(helper)]() {
        nsresult rv;
        nsCOMPtr<nsIMediaDrmProvisioning> provisioning = do_CreateInstance(
            "@mozilla.org/dom/media/eme/mediadrm/provisioning;1", &rv);
        if (!provisioning) {
          helper->MaybeResolve(MediaResult(
              NS_ERROR_DOM_INVALID_STATE_ERR,
              "Failed to create nsIMediaDrmProvisioning object"_ns));
          return;
        }

        NS_ConvertUTF8toUTF16 requestData(
            reinterpret_cast<const char*>(request.request().Elements()),
            request.request().Length());

        RefPtr<dom::Promise> promise;
        rv = provisioning->Provision(request.serverUrl(), requestData,
                                     getter_AddRefs(promise));
        if (NS_FAILED(rv)) {
          helper->MaybeResolve(MediaResult(
              NS_ERROR_DOM_INVALID_STATE_ERR,
              "nsICDMProvisioning::ProvisionAMediaDrm call failed"_ns));
          return;
        }

        promise->AppendNativeHandler(helper);
      }));
#  else
  aResolver(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR));
#  endif
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyStatus(
    const RemoteCDMKeyStatusIPDL& aMsg) {
  bool changed = false;
  {
    auto caps = mCapabilites.Lock();
    for (const auto& keyInfo : aMsg.keyInfo()) {
      changed |=
          caps->SetKeyStatus(keyInfo.mKeyId, aMsg.sessionId(), keyInfo.mStatus);
    }
  }

  if (!changed) {
    return IPC_OK();
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = aMsg.sessionId()]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(sessionId)) {
          session->DispatchKeyStatusesChange();
        }
      }));
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyExpiration(
    RemoteCDMKeyExpirationIPDL&& aMsg) {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, msg = std::move(aMsg)]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(msg.sessionId())) {
          session->SetExpiration(msg.expiredTimeMilliSecondsSinceEpoch());
        }
      }));
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyMessage(
    RemoteCDMKeyMessageIPDL&& aMsg) {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, msg = std::move(aMsg)]() {
        if (self->mKeys.IsNull()) {
          return;
        }
        if (RefPtr<dom::MediaKeySession> session =
                self->mKeys->GetSession(msg.sessionId())) {
          session->DispatchKeyMessage(msg.type(), msg.message());
        }
      }));
  return IPC_OK();
}

void RemoteCDMChild::Init(PromiseId aPromiseId, const nsAString& aOrigin,
                          const nsAString& aTopLevelOrigin,
                          const nsAString& aName) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(
      NS_NewRunnableFunction(__func__, [self = RefPtr{this}, aPromiseId]() {
        self->SendInit(
                RemoteCDMInitRequestIPDL(self->mDistinctiveIdentifierRequired,
                                         self->mPersistentStateRequired))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self,
                 aPromiseId](const InitPromise::ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                                    "PRemoteCDMChild::SendInit IPC fail"_ns));
                    return;
                  }

                  self->mKeys->OnCDMCreated(aPromiseId, 0);
                });
      })));
}

void RemoteCDMChild::CreateSession(uint32_t aCreateSessionToken,
                                   MediaKeySessionType aSessionType,
                                   PromiseId aPromiseId,
                                   const nsAString& aInitDataType,
                                   nsTArray<uint8_t>& aInitData) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, aCreateSessionToken, aSessionType,
                 aPromiseId, initDataType = nsString(aInitDataType),
                 initData = std::move(aInitData)]() mutable {
        self
            ->SendCreateSession(RemoteCDMCreateSessionRequestIPDL(
                aSessionType, std::move(initDataType), std::move(initData)))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aCreateSessionToken, aPromiseId](
                    const CreateSessionPromise::ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMChild::SendCreateSession IPC fail"_ns));
                    return;
                  }

                  const auto& response = aValue.ResolveValue();
                  if (response.type() ==
                      RemoteCDMSessionResponseIPDL::TMediaResult) {
                    self->RejectPromise(aPromiseId, response.get_MediaResult());
                    return;
                  }

                  const auto& sessionId = response.get_nsString();
                  if (RefPtr<dom::MediaKeySession> session =
                          self->mKeys->GetPendingSession(aCreateSessionToken)) {
                    session->SetSessionId(sessionId);
                  }

                  self->ResolvePromise(aPromiseId);
                });
      })));
}

void RemoteCDMChild::LoadSession(PromiseId aPromiseId,
                                 dom::MediaKeySessionType aSessionType,
                                 const nsAString& aSessionId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, aPromiseId, aSessionType,
                 sessionId = nsString(aSessionId)]() mutable {
        self->SendLoadSession(RemoteCDMLoadSessionRequestIPDL(
                                  aSessionType, std::move(sessionId)))
            ->Then(GetMainThreadSerialEventTarget(), __func__,
                   [self, aPromiseId](
                       const LoadSessionPromise::ResolveOrRejectValue& aValue) {
                     if (self->mKeys.IsNull()) {
                       return;
                     }

                     self->mKeys->OnSessionLoaded(
                         aPromiseId, aValue.IsResolve() &&
                                         NS_SUCCEEDED(aValue.ResolveValue()));
                   });
      })));
}

void RemoteCDMChild::SetServerCertificate(PromiseId aPromiseId,
                                          nsTArray<uint8_t>& aCert) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__,
      [self = RefPtr{this}, aPromiseId, cert = std::move(aCert)]() mutable {
        self->SendSetServerCertificate(std::move(cert))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](
                    const SetServerCertificatePromise::ResolveOrRejectValue&
                        aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMChild::SendSetServerCertificate IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMChild::UpdateSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId,
                                   nsTArray<uint8_t>& aResponse) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId, response = std::move(aResponse)]() mutable {
        self->SendUpdateSession(RemoteCDMUpdateSessionRequestIPDL(
                                    std::move(sessionId), std::move(response)))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](
                    const UpdateSessionPromise::ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMChild::SendUpdateSession IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMChild::CloseSession(const nsAString& aSessionId,
                                  PromiseId aPromiseId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId]() mutable {
        self->SendCloseSession(std::move(sessionId))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](
                    const CloseSessionPromise::ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMChild::SendCloseSession IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMChild::RemoveSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId) {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}, sessionId = nsString(aSessionId),
                 aPromiseId]() mutable {
        self->SendRemoveSession(std::move(sessionId))
            ->Then(
                GetMainThreadSerialEventTarget(), __func__,
                [self, aPromiseId](
                    const RemoveSessionPromise::ResolveOrRejectValue& aValue) {
                  if (self->mKeys.IsNull()) {
                    return;
                  }

                  if (aValue.IsReject()) {
                    self->RejectPromise(
                        aPromiseId,
                        MediaResult(
                            NS_ERROR_DOM_INVALID_STATE_ERR,
                            "PRemoteCDMChild::SendRemoveSession IPC fail"_ns));
                    return;
                  }

                  self->ResolveOrRejectPromise(aPromiseId,
                                               aValue.ResolveValue());
                });
      })));
}

void RemoteCDMChild::QueryOutputProtectionStatus() {}

void RemoteCDMChild::NotifyOutputProtectionStatus(
    OutputProtectionCheckStatus aCheckStatus,
    OutputProtectionCaptureStatus aCaptureStatus) {}

void RemoteCDMChild::Shutdown() {
  MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}] { self->Send__delete__(self); })));
}

void RemoteCDMChild::Terminated() {}

void RemoteCDMChild::OnSetSessionId(uint32_t aCreateSessionToken,
                                    const nsAString& aSessionId) {}

void RemoteCDMChild::OnResolveLoadSessionPromise(uint32_t aPromiseId,
                                                 bool aSuccess) {}

void RemoteCDMChild::OnSessionMessage(const nsAString& aSessionId,
                                      dom::MediaKeyMessageType aMessageType,
                                      const nsTArray<uint8_t>& aMessage) {}

void RemoteCDMChild::OnExpirationChange(const nsAString& aSessionId,
                                        UnixTime aExpiryTime) {}

void RemoteCDMChild::OnSessionClosed(const nsAString& aSessionId) {}

void RemoteCDMChild::OnSessionError(const nsAString& aSessionId,
                                    nsresult aException, uint32_t aSystemCode,
                                    const nsAString& aMsg) {}

void RemoteCDMChild::OnRejectPromise(uint32_t aPromiseId,
                                     ErrorResult&& aException,
                                     const nsCString& aMsg) {}

RefPtr<DecryptPromise> RemoteCDMChild::Decrypt(MediaRawData* aSample) {
  return nullptr;
}

void RemoteCDMChild::OnDecrypted(uint32_t aId, DecryptStatus aResult,
                                 const nsTArray<uint8_t>& aDecryptedData) {}

void RemoteCDMChild::RejectPromise(PromiseId aId, ErrorResult&& aException,
                                   const nsCString& aReason) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mKeys.IsNull());
  mKeys->RejectPromise(aId, std::move(aException), aReason);
}

void RemoteCDMChild::ResolvePromise(PromiseId aId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mKeys.IsNull());
  mKeys->ResolvePromise(aId);
}

void RemoteCDMChild::RejectPromise(PromiseId aId, const MediaResult& aResult) {
  MOZ_ASSERT(NS_FAILED(aResult.Code()));

  ErrorResult rv;
  aResult.ThrowTo(rv);
  RejectPromise(aId, std::move(rv), aResult.Message());
}

void RemoteCDMChild::ResolveOrRejectPromise(PromiseId aId,
                                            const MediaResult& aResult) {
  if (aResult.Code() == NS_OK) {
    ResolvePromise(aId);
    return;
  }

  RejectPromise(aId, aResult);
}

void RemoteCDMChild::OnKeyStatusesChange(const nsAString& aSessionId) {}

void RemoteCDMChild::GetStatusForPolicy(
    PromiseId aPromiseId, const dom::HDCPVersion& aMinHdcpVersion) {
  RejectPromise(aPromiseId, MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR));
}

#ifdef DEBUG
bool RemoteCDMChild::IsOnOwnerThread() { return mThread->IsOnCurrentThread(); }
#endif

}  // namespace mozilla
