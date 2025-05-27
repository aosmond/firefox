/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteCDMChild.h"

namespace mozilla {

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
    const RemoteCDMProvisionRequestIPDL& request,
    ProvisionResolver&& aResolver) {
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyStatus(
    const RemoteCDMKeyStatusIPDL& msg) {
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyExpiration(
    const RemoteCDMKeyExpirationIPDL& msg) {
  return IPC_OK();
}
mozilla::ipc::IPCResult RemoteCDMChild::RecvOnSessionKeyMessage(
    const RemoteCDMKeyMessageIPDL& msg) {
  return IPC_OK();
}

void RemoteCDMChild::Init(PromiseId aPromiseId, const nsAString& aOrigin,
                          const nsAString& aTopLevelOrigin,
                          const nsAString& aName) {}

void RemoteCDMChild::CreateSession(uint32_t aCreateSessionToken,
                                   MediaKeySessionType aSessionType,
                                   PromiseId aPromiseId,
                                   const nsAString& aInitDataType,
                                   nsTArray<uint8_t>& aInitData) {}

void RemoteCDMChild::LoadSession(PromiseId aPromiseId,
                                 dom::MediaKeySessionType aSessionType,
                                 const nsAString& aSessionId) {}

void RemoteCDMChild::SetServerCertificate(PromiseId aPromiseId,
                                          nsTArray<uint8_t>& aCert) {}

void RemoteCDMChild::UpdateSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId,
                                   nsTArray<uint8_t>& aResponse) {}

void RemoteCDMChild::CloseSession(const nsAString& aSessionId,
                                  PromiseId aPromiseId) {}

void RemoteCDMChild::RemoveSession(const nsAString& aSessionId,
                                   PromiseId aPromiseId) {}

void RemoteCDMChild::QueryOutputProtectionStatus() {}

void RemoteCDMChild::NotifyOutputProtectionStatus(
    OutputProtectionCheckStatus aCheckStatus,
    OutputProtectionCaptureStatus aCaptureStatus) {}

void RemoteCDMChild::Shutdown() {}

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
                                   const nsCString& aReason) {}

void RemoteCDMChild::ResolvePromise(PromiseId aId) {}

void RemoteCDMChild::OnKeyStatusesChange(const nsAString& aSessionId) {}

void RemoteCDMChild::GetStatusForPolicy(
    PromiseId aPromiseId, const dom::HDCPVersion& aMinHdcpVersion) {}

#ifdef DEBUG
bool RemoteCDMChild::IsOnOwnerThread() { return false; }
#endif

}  // namespace mozilla
