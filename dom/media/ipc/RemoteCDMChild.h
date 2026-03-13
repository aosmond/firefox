/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteCDMChild_h
#define include_dom_media_ipc_RemoteCDMChild_h

#include "mozilla/PRemoteCDMChild.h"

namespace mozilla {

class RemoteCDMProxy;

/**
 * This class implements the content process actor for managing CDM instances in
 * a remote process performing the decoding/decrypting. It is created via
 * RemoteMediaManagerChild::CreateCDM. It destroys itself when there is a single
 * reference left (the IPDL reference to the actor). The CDMProxy methods are
 * threadsafe and dispatch to the RemoteMediaManagerChild IPDL thread.
 *
 * To provide a remote implementation in another process, one must subclass
 * RemoteCDMParent and ensure the correct actor class is created in
 * RemoteMediaManagerParent::AllocPRemoteCDMParent.
 *
 * Remote decoders are supplied the PRemoteCDMActor pointer for encrypted media,
 * which they can integrate with depending on the particular CDM API.
 */
class RemoteCDMChild final : public PRemoteCDMChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteCDMChild, final);

  RemoteCDMChild();

  void Initialize(RemoteCDMProxy* aProxy);
  void Destroy();

  // PRemoteCDMChild
  void ActorDestroy(ActorDestroyReason aWhy) override;
  mozilla::ipc::IPCResult RecvProvision(
      const RemoteCDMProvisionRequestIPDL& aRequest,
      ProvisionResolver&& aResolver);
  mozilla::ipc::IPCResult RecvOnSessionKeyStatus(
      const RemoteCDMKeyStatusIPDL& aMsg);
  mozilla::ipc::IPCResult RecvOnSessionKeyExpiration(
      RemoteCDMKeyExpirationIPDL&& aMsg);
  mozilla::ipc::IPCResult RecvOnSessionKeyMessage(
      RemoteCDMKeyMessageIPDL&& aMsg);

 private:
  virtual ~RemoteCDMChild();

  RefPtr<RemoteCDMProxy> mProxy;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteCDMChild_h
