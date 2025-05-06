/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef include_dom_media_ipc_RemoteEncoderChild_h
#define include_dom_media_ipc_RemoteEncoderChild_h

#include <functional>

#include "mozilla/PRemoteEncoderChild.h"
#include "mozilla/RemoteMediaManagerChild.h"
#include "mozilla/ShmemRecycleAllocator.h"

namespace mozilla {

class RemoteMediaManagerChild;
using mozilla::MediaDataEncoder;
using mozilla::ipc::IPCResult;

class RemoteMediaDataEncoderChild : public ShmemRecycleAllocator<RemoteEncoderChild>,
                                    public PRemoteEncoderChild {
  friend class PRemoteEncoderChild;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteMediaEncoderChild, final);

  explicit RemoteEncoderChild(RemoteMediaIn aLocation);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  // This interface closely mirrors the MediaDataEncoder plus a bit
  // (DestroyIPDL) to allow proxying to a remote decoder in RemoteEncoderModule.
  RefPtr<MediaDataEncoder::InitPromise> Init();
  RefPtr<MediaDataEncoder::EncodePromise> Encode(
      const nsTArray<RefPtr<MediaData>>& aSamples);
  RefPtr<MediaDataEncoder::EncodePromise> Drain();
  RefPtr<MediaDataEncoder::ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>&
          aConfigurationChanges);
  RefPtr<mozilla::ShutdownPromise> Shutdown();
  bool IsHardwareAccelerated(nsACString& aFailureReason) const;
  nsCString GetDescriptionName() const;
  nsCString GetProcessName() const;
  nsCString GetCodecName() const;
  void SetBitrate(uint32_t aBitsPerSec);
  void DestroyIPDL();

  // Called from IPDL when our actor has been destroyed
  void IPDLActorDestroyed();

  RemoteMediaManagerChild* GetManager();

 protected:
  virtual ~RemoteEncoderChild();
  void AssertOnManagerThread() const;

  virtual MediaResult ProcessOutput(ArrayOfRemoteMediaRawData&& aEncodedData) = 0;
  virtual void RecordShutdownTelemetry(bool aForAbnormalShutdown) {}

  RefPtr<RemoteEncoderChild> mIPDLSelfRef;
  nsTArray<RefPtr<MediaRawData>> mEncodedData;
  const RemoteMediaIn mLocation;

 private:
  const nsCOMPtr<nsISerialEventTarget> mThread;

  MozPromiseHolder<MediaDataEncoder::InitPromise> mInitPromise;
  MozPromiseRequestHolder<PRemoteEncoderChild::InitPromise> mInitPromiseRequest;
  MozPromiseHolder<MediaDataEncoder::EncodePromise> mEncodePromise;
  MozPromiseHolder<MediaDataEncoder::EncodePromise> mDrainPromise;
  MozPromiseHolder<MediaDataEncoder::ReconfigurationPromise>
      mReconfigurationPromise;
  MozPromiseHolder<mozilla::ShutdownPromise> mShutdownPromise;

  void HandleRejectionError(
      const ipc::ResponseRejectReason& aReason,
      std::function<void(const MediaResult&)>&& aCallback);

  nsCString mHardwareAcceleratedReason;
  nsCString mDescription;
  nsCString mProcessName;
  nsCString mCodecName;
  bool mIsHardwareAccelerated = false;
  bool mRemoteEncoderCrashed = false;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteEncoderChild_h
