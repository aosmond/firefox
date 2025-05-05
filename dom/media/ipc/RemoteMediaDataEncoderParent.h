/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteMediaDataEncoderParent_h
#define include_dom_media_ipc_RemoteMediaDataEncoderParent_h

#include "mozilla/PRemoteEncoderParent.h"
#include "mozilla/ShmemRecycleAllocator.h"
#include "PlatformEncoderModule.h"

namespace mozilla {

class RemoteMediaManagerParent;
using mozilla::ipc::IPCResult;

class RemoteMediaDataEncoderParent
    : public ShmemRecycleAllocator<RemoteMediaDataEncoderParent>,
      public PRemoteEncoderParent {
  friend class PRemoteEncoderParent;

 public:
  // We refcount this class since the task queue can have runnables
  // that reference us.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteMediaDataEncoderParent, final)

  RemoteMediaDataEncoderParent(RemoteMediaManagerParent* aParent,
                               nsISerialEventTarget* aManagerThread,
                               TaskQueue* aEncodeTaskQueue);

  void Destroy();

  // PRemoteEncoderParent
  virtual IPCResult RecvConstruct(ConstructResolver&& aResolver) = 0;
  IPCResult RecvInit(InitResolver&& aResolver);
  IPCResult RecvEncode(const EncodedInputIPDL& aData,
                       EncodeResolver&& aResolver);
  IPCResult RecvReconfigure(
      EncoderConfigurationChangeList* aConfigurationChanges,
      ReconfigureResolver&& aResolver);
  IPCResult RecvDrain(DrainResolver&& aResolver);
  IPCResult RecvShutdown(ShutdownResolver&& aResolver);
  IPCResult RecvSetBitrate(const uint32_t& aBitrate);

  void ActorDestroy(ActorDestroyReason aWhy) override;

 protected:
  virtual ~RemoteMediaDataEncoderParent();

  bool OnManagerThread();

#if 0
  virtual MediaResult ProcessDecodedData(MediaDataDecoder::DecodedData&& aData,
                                         DecodedOutputIPDL& aDecodedData) = 0;
#endif

  const RefPtr<RemoteMediaManagerParent> mParent;
  const RefPtr<TaskQueue> mDecodeTaskQueue;
  RefPtr<MediaDataEncoder> mEncoder;

 private:
  const RefPtr<nsISerialEventTarget> mManagerThread;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteMediaDataEncoderParent_h
