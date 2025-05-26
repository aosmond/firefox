/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteCDMChild_h
#define include_dom_media_ipc_RemoteCDMChild_h

#include "mozilla/PRemoteCDMChild.h"
#include "mozilla/RemoteMediaManagerChild.h"

namespace mozilla {

class RemoteCDMChild final : public PRemoteCDMChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteCDMChild, final);

  RemoteCDMChild(nsCOMPtr<nsISerialEventTarget>&& aThread,
                 RemoteMediaIn aLocation);

  nsISerialEventTarget* GetManagerThread() const { return mThread; }
  RemoteMediaIn GetLocation() const { return mLocation; }

  void ActorDestroy(ActorDestroyReason aWhy) override;

  // PRemoteCDMChild
  mozilla::ipc::IPCResult RecvProvision(
      const RemoteCDMProvisionRequestIPDL& request,
      ProvisionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvOnSessionKeyStatus(
      const RemoteCDMKeyStatusIPDL& msg);

  mozilla::ipc::IPCResult RecvOnSessionKeyExpiration(
      const RemoteCDMKeyExpirationIPDL& msg);

  mozilla::ipc::IPCResult RecvOnSessionKeyMessage(
      const RemoteCDMKeyMessageIPDL& msg);

 private:
  virtual ~RemoteCDMChild();
  RemoteMediaManagerChild* GetManager();

  void MaybeDestroyActor();

  const nsCOMPtr<nsISerialEventTarget> mThread;
  const RemoteMediaIn mLocation;
  bool mRemoteCrashed = false;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteCDMChild_h
