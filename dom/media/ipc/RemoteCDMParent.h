/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteCDMParent_h
#define include_dom_media_ipc_RemoteCDMParent_h

#include "mozilla/PRemoteCDMParent.h"

namespace mozilla {

class RemoteCDMParent final : public PRemoteCDMParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteCDMParent, final);

  RemoteCDMParent();

  // PRemoteCDMParent
  mozilla::ipc::IPCResult RecvInit(const RemoteCDMInitRequestIPDL& request,
                                   InitResolver&& aResolver);

  mozilla::ipc::IPCResult RecvCreateSession(
      const RemoteCDMCreateSessionRequestIPDL& request,
      CreateSessionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvLoadSession(
      const RemoteCDMLoadSessionRequestIPDL& request,
      LoadSessionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvUpdateSession(
      const RemoteCDMUpdateSessionRequestIPDL& request,
      UpdateSessionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvRemoveSession(const nsAString& sessionId,
                                            RemoveSessionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvCloseSession(const nsAString& sessionId,
                                           CloseSessionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvSetServerCertificate(
      mozilla::Span<uint8_t const> certificate,
      SetServerCertificateResolver&& aResolver);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  virtual ~RemoteCDMParent();
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteCDMParent_h
