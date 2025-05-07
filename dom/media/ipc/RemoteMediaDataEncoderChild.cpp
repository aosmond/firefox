/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoderChild.h"
#include "RemoteMediaManagerChild.h"
#include "RemoteDecodeUtils.h"

namespace mozilla {

RemoteMediaDataEncoderChild::RemoteMediaDataEncoderChild(
    RemoteMediaIn aLocation)
    : ShmemRecycleAllocator(this), mLocation(aLocation) {}

void RemoteMediaDataEncoderChild::MaybeDestroyActor() {
  // If this is the last reference, and we still have an actor, then we know
  // that the last reference is solely due to the IPDL reference. Dispatch to
  // the owning thread to delete that so that we can clean up.
  MutexAutoLock lock(mMutex);
  if (mHasActor) {
    mHasActor = false;
    mThread->Dispatch(NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
      if (self->CanSend()) {
        self->Send__delete__(self);
      }
    }));
  }
}

void RemoteMediaDataEncoderChild::ActorDestroy(ActorDestroyReason aWhy) {
  MutexAutoLock lock(mMutex);
  mHasActor = false;
}

RefPtr<MediaDataEncoder::InitPromise> RemoteMediaDataEncoderChild::Init() {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  return InvokeAsync(
      mThread, __func__, [self]() -> RefPtr<MediaDataEncoder::InitPromise> {
        self->SendInit()->Then(
            self->mThread, __func__,
            [self](EncodeInitResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeInitResultIPDL::TMediaResult) {
                self->mInitPromise.Reject(aResponse.get_MediaResult(),
                                          __func__);
                return;
              }

              const auto& initResponse =
                  aResponse.get_EncodeInitCompletionIPDL();

              MutexAutoLock lock(self->mMutex);
              self->mDescription = initResponse.description();
              self->mDescription.Append(" (");
              self->mDescription.Append(
                  RemoteMediaInToStr(self->GetManager()->Location()));
              self->mDescription.Append(" remote)");

              self->mProcessName = initResponse.processName();
              self->mCodecName = initResponse.codecName();

              self->mIsHardwareAccelerated = initResponse.hardware();
              self->mHardwareAcceleratedReason = initResponse.hardwareReason();
              self->mInitPromise.ResolveIfExists(true, __func__);
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              self->GetManager()->HandleRejectionError(
                  aReason, [self](const MediaResult& aError) {
                    self->mInitPromise.RejectIfExists(aError, __func__);
                  });
            });
        return self->mInitPromise.Ensure(__func__);
      });
}

RefPtr<PRemoteEncoderChild::EncodePromise>
RemoteMediaDataEncoderChild::DoSendEncode(const MediaData* aSample) {
  if (aSample->mType == MediaData::Type::AUDIO_DATA) {
    auto samples = MakeRefPtr<ArrayOfRemoteAudioData>();
    if (!samples->Fill(aSample->As<const AudioData>(),
                       [&](size_t aSize) { return AllocateBuffer(aSize); })) {
      return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
          MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
    }
    return SendEncode(std::move(samples));
#if 0
  } else if (aSample->mType == MediaData::Type::VIDEO_DATA) {
    // TODO(aosmond): If the location matches the SurfaceDescriptorGPUVideo location, then we can just use the descriptor. Otherwise we do the readback and copy into a shmem.
    auto samples = MakeRefPtr<ArrayOfRemoteVideoData>();
    if (!samples->Fill(aSample->As<const VideoData>(),
                       mLocation,
                       [&](size_t aSize) { return AllocateBuffer(aSize); })) {
      return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
          MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
    }
    return SendEncode(std::move(samples));
#endif
  } else {
    return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
        MediaResult(NS_ERROR_INVALID_ARG), __func__);
  }
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoderChild::Encode(
    const MediaData* aSample) {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  RefPtr<const MediaData> sample = aSample;
  return InvokeAsync(
      mThread, __func__,
      [self, sample]() -> RefPtr<MediaDataEncoder::EncodePromise> {
        self->DoSendEncode(sample)->Then(
            self->mThread, __func__,
            [self](EncodeResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                self->mDrainPromise.Reject(aResponse.get_MediaResult(),
                                           __func__);
                return;
              }

              nsTArray<RefPtr<MediaRawData>> samples;
              if (auto remoteSamples =
                      aResponse.get_ArrayOfRemoteMediaRawData()) {
                size_t count = remoteSamples->Count();
                samples.SetCapacity(count);
                for (size_t i = 0; i < count; ++i) {
                  if (RefPtr<MediaRawData> sample =
                          remoteSamples->ElementAt(i)) {
                    samples.AppendElement(std::move(sample));
                  } else {
                    self->mDrainPromise.Reject(
                        MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
                    return;
                  }
                }
              }

              self->mDrainPromise.Resolve(std::move(samples), __func__);
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              self->GetManager()->HandleRejectionError(
                  aReason, [self](const MediaResult& aError) {
                    self->mDrainPromise.RejectIfExists(aError, __func__);
                  });
            });
        return self->mDrainPromise.Ensure(__func__);
      });
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoderChild::Drain() {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  return InvokeAsync(
      mThread, __func__, [self]() -> RefPtr<MediaDataEncoder::EncodePromise> {
        self->SendDrain()->Then(
            self->mThread, __func__,
            [self](EncodeResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                self->mDrainPromise.Reject(aResponse.get_MediaResult(),
                                           __func__);
                return;
              }

              nsTArray<RefPtr<MediaRawData>> samples;
              if (auto remoteSamples =
                      aResponse.get_ArrayOfRemoteMediaRawData()) {
                size_t count = remoteSamples->Count();
                samples.SetCapacity(count);
                for (size_t i = 0; i < count; ++i) {
                  if (RefPtr<MediaRawData> sample =
                          remoteSamples->ElementAt(i)) {
                    samples.AppendElement(std::move(sample));
                  } else {
                    self->mDrainPromise.Reject(
                        MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
                    return;
                  }
                }
              }

              self->mDrainPromise.Resolve(std::move(samples), __func__);
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              self->GetManager()->HandleRejectionError(
                  aReason, [self](const MediaResult& aError) {
                    self->mDrainPromise.RejectIfExists(aError, __func__);
                  });
            });
        return self->mDrainPromise.Ensure(__func__);
      });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
RemoteMediaDataEncoderChild::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  return InvokeAsync(
      mThread, __func__,
      [self, changes = RefPtr{aConfigurationChanges}]()
          -> RefPtr<MediaDataEncoder::ReconfigurationPromise> {
        self->SendReconfigure(
                const_cast<EncoderConfigurationChangeList*>(changes.get()))
            ->Then(
                self->mThread, __func__,
                [self](const MediaResult& aResult) {
                  if (NS_SUCCEEDED(aResult)) {
                    self->mReconfigurePromise.ResolveIfExists(true, __func__);
                  } else {
                    self->mReconfigurePromise.RejectIfExists(aResult, __func__);
                  }
                },
                [self](const mozilla::ipc::ResponseRejectReason& aReason) {
                  self->GetManager()->HandleRejectionError(
                      aReason, [self](const MediaResult& aError) {
                        self->mReconfigurePromise.RejectIfExists(aError,
                                                                 __func__);
                      });
                });
        return self->mReconfigurePromise.Ensure(__func__);
      });
}

RefPtr<mozilla::ShutdownPromise> RemoteMediaDataEncoderChild::Shutdown() {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  return InvokeAsync(
      mThread, __func__, [self]() -> RefPtr<mozilla::ShutdownPromise> {
        self->SendShutdown()->Then(
            self->mThread, __func__,
            [self](PRemoteEncoderChild::ShutdownPromise::ResolveOrRejectValue&&
                       aValue) {
              if (self->CanSend()) {
                self->Send__delete__(self);
              }
              self->mShutdownPromise.Resolve(aValue.IsResolve(), __func__);
            });
        return self->mShutdownPromise.Ensure(__func__);
      });
}

bool RemoteMediaDataEncoderChild::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  return false;
}

nsCString RemoteMediaDataEncoderChild::GetDescriptionName() const {
  return ""_ns;
}

nsCString RemoteMediaDataEncoderChild::GetProcessName() const { return ""_ns; }

nsCString RemoteMediaDataEncoderChild::GetCodecName() const { return ""_ns; }

RefPtr<GenericPromise> RemoteMediaDataEncoderChild::SetBitrate(
    uint32_t aBitsPerSec) {
  RefPtr<RemoteMediaDataEncoderChild> self = this;
  return InvokeAsync(mThread, __func__,
                     [self, aBitsPerSec]() -> RefPtr<GenericPromise> {
                       self->SendSetBitrate(aBitsPerSec);
                       return GenericPromise::CreateAndResolve(true, __func__);
                     });
}

RemoteMediaManagerChild* RemoteMediaDataEncoderChild::GetManager() {
  if (!CanSend()) {
    return nullptr;
  }
  return static_cast<RemoteMediaManagerChild*>(Manager());
}

RemoteMediaDataEncoderChild::~RemoteMediaDataEncoderChild() = default;

void RemoteMediaDataEncoderChild::AssertOnManagerThread() const {}

void RemoteMediaDataEncoderChild::RecordShutdownTelemetry(
    bool aForAbnormalShutdown) {}

}  // namespace mozilla
