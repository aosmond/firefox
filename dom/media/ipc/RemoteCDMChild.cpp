/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteCDMChild.h"
#include "mozilla/dom/MediaKeySession.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/MediaDrmProvisioningHelper.h"
#endif

namespace mozilla {

RemoteCDMChild::RemoteCDMChild(
    nsCOMPtr<nsISerialEventTarget>&& aThread,
    RefPtr<GenericNonExclusivePromise>&& aIPDLPromise, RemoteMediaIn aLocation,
    dom::MediaKeys* aKeys, const nsAString& aKeySystem,
    bool aDistinctiveIdentifierRequired, bool aPersistentStateRequired)
    : CDMProxy(aKeys, aKeySystem, aDistinctiveIdentifierRequired,
               aPersistentStateRequired),
      mMutex("RemoteCDMChild::mMutex"),
      mThread(std::move(aThread)),
      mIPDLPromise(std::move(aIPDLPromise)),
      mLocation(aLocation) {}

RemoteCDMChild::~RemoteCDMChild() = default;

RemoteMediaManagerChild* RemoteCDMChild::GetManager() { return nullptr; }

void RemoteCDMChild::ActorDestroy(ActorDestroyReason aWhy) {
  mNeedsShutdown = false;
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvProvision(
    const RemoteCDMProvisionRequestIPDL& aRequest,
    ProvisionResolver&& aResolver) {
#  ifdef MOZ_WIDGET_ANDROID
  auto helper =
      MakeRefPtr<MediaDrmProvisioningHelper>(aRequest, std::move(aResolver));
  helper->Provision();
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
  MutexAutoLock lock(mMutex);
  if (!mIPDLPromise) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild already initialized"_ns));
    return;
  }

  mIPDLPromise->Then(
      mThread, __func__,
      [self = RefPtr{this}, aPromiseId](
          const GenericNonExclusivePromise::ResolveOrRejectValue& aValue) {
        if (self->mKeys.IsNull()) {
          return;
        }

        if (aValue.IsReject()) {
          self->RejectPromise(
              aPromiseId,
              MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                          "PRemoteCDMChild ensure process fail"_ns));
          return;
        }

        self->InitInternal(aPromiseId);
      });
  mIPDLPromise = nullptr;
}

void RemoteCDMChild::InitInternal(PromiseId aPromiseId) {
  RefPtr<RemoteMediaManagerChild> manager =
      RemoteMediaManagerChild::GetSingleton(mLocation);
  if (!manager) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild manager is not available"_ns));
    return;
  }

  if (!manager->SendPRemoteCDMConstructor(this, mKeySystem)) {
    RejectPromise(aPromiseId,
                  MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                              "PRemoteCDMChild manager is unable to send"_ns));
    return;
  }

  SendInit(RemoteCDMInitRequestIPDL(mDistinctiveIdentifierRequired,
                                    mPersistentStateRequired))
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [self = RefPtr{this},
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
  // If this is the last reference, and we still have an actor, then we know
  // that the last reference is solely due to the IPDL reference. Dispatch to
  // the owning thread to delete that so that we can clean up.
  if (mNeedsShutdown.exchange(false)) {
    mThread->Dispatch(NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
      if (self->CanSend()) {
        self->Send__delete__(self);
      }
    }));
  }
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
