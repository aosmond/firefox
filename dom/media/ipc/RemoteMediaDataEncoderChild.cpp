/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoderChild.h"
#include "RemoteMediaManagerChild.h"
#include "RemoteDecodeUtils.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#define LOGE(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Error, \
          ("[RemoteMediaDataEncoderChild] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGW(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Warning, \
          ("[RemoteMediaDataEncoderChild] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGD(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Debug, \
          ("[RemoteMediaDataEncoderChild] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGV(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Verbose, \
          ("[RemoteMediaDataEncoderChild] %s: " fmt, __func__, ##__VA_ARGS__))

RemoteMediaDataEncoderChild::RemoteMediaDataEncoderChild(
    nsCOMPtr<nsISerialEventTarget>&& aThread, RemoteMediaIn aLocation)
    : ShmemRecycleAllocator(this),
      mThread(std::move(aThread)),
      mLocation(aLocation) {
  LOGV("[%p]", this);
}

RemoteMediaDataEncoderChild::~RemoteMediaDataEncoderChild() {
  LOGV("[%p]", this);
}

void RemoteMediaDataEncoderChild::MaybeDestroyActor() {
  // If this is the last reference, and we still have an actor, then we know
  // that the last reference is solely due to the IPDL reference. Dispatch to
  // the owning thread to delete that so that we can clean up.
  MutexAutoLock lock(mMutex);
  if (mNeedsShutdown) {
    mNeedsShutdown = false;
    mThread->Dispatch(NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
      if (self->CanSend()) {
        LOGD("[%p] destroying final self reference", self.get());
        self->Send__delete__(self);
      }
    }));
  }
}

void RemoteMediaDataEncoderChild::ActorDestroy(ActorDestroyReason aWhy) {
  LOGD("[%p]", this);

  {
    MutexAutoLock lock(mMutex);
    mNeedsShutdown = false;
  }

  mRemoteCrashed = aWhy == ActorDestroyReason::AbnormalShutdown;
  CleanupShmemRecycleAllocator();
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
RemoteMediaDataEncoderChild::Construct() {
  {
    MutexAutoLock lock(mMutex);
    mNeedsShutdown = CanSend();
  }

  LOGD("[%p] send", this);
  SendConstruct()->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaResult aResult) {
        LOGD("[%p] Construct resolved code=0x%08x", self.get(), aResult.Code());
        self->mConstructPromise.Resolve(self, __func__);
        if (!self->mInitPromise.IsEmpty()) {
          self->Init();
        }
      },
      [self = RefPtr{this}](const mozilla::ipc::ResponseRejectReason& aReason) {
        LOGE("[%p] Construct ipc failed", self.get());
        self->GetManager()->HandleRejectionError(
            aReason, [self](const MediaResult& aError) {
              self->mConstructPromise.RejectIfExists(aError, __func__);
              self->mInitPromise.RejectIfExists(aError, __func__);
            });
      });
  return mConstructPromise.Ensure(__func__);
}

RefPtr<MediaDataEncoder::InitPromise> RemoteMediaDataEncoderChild::Init() {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<MediaDataEncoder::InitPromise> {
        // If the owner called Init before the Construct response, then just
        // create promise and wait for that first. This can happen if the owner
        // created the encoder via RemoteEncoderModule's CreateAudioEncoder or
        // CreateVideoEncoder instead of AsyncCreateEncoder.
        if (!self->mConstructPromise.IsEmpty()) {
          LOGD("[%p] Init deferred, still constructing", self.get());
          return self->mInitPromise.Ensure(__func__);
        }

        LOGD("[%p] Init send", self.get());
        self->SendInit()->Then(
            self->mThread, __func__,
            [self](EncodeInitResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeInitResultIPDL::TMediaResult) {
                LOGE("[%p] Init resolved code=0x%08x", self.get(),
                     aResponse.get_MediaResult().Code());
                self->mInitPromise.Reject(aResponse.get_MediaResult(),
                                          __func__);
                return;
              }

              const auto& initResponse =
                  aResponse.get_EncodeInitCompletionIPDL();

              LOGD("[%p] Init resolved hwAccel=%d desc=\"%s\"", self.get(),
                   initResponse.hardware(), initResponse.description().get());
              MutexAutoLock lock(self->mMutex);
              self->mDescription = initResponse.description();
              self->mDescription.Append(" (");
              self->mDescription.Append(
                  RemoteMediaInToStr(self->GetManager()->Location()));
              self->mDescription.Append(" remote)");

              self->mIsHardwareAccelerated = initResponse.hardware();
              self->mHardwareAcceleratedReason = initResponse.hardwareReason();
              self->mInitPromise.ResolveIfExists(true, __func__);
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              LOGE("[%p] Init ipc failed", self.get());
              self->GetManager()->HandleRejectionError(
                  aReason, [self](const MediaResult& aError) {
                    self->mInitPromise.RejectIfExists(aError, __func__);
                  });
            });
        return self->mInitPromise.Ensure(__func__);
      });
}

RefPtr<PRemoteEncoderChild::EncodePromise>
RemoteMediaDataEncoderChild::DoSendEncode(const MediaData* aSample,
                                          ShmemRecycleTicket* aTicket) {
  if (mRemoteCrashed) {
    nsresult err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_UTILITY_ERR;
    if (mLocation == RemoteMediaIn::GpuProcess ||
        mLocation == RemoteMediaIn::RddProcess) {
      err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR;
    } else if (mLocation == RemoteMediaIn::UtilityProcess_MFMediaEngineCDM) {
      err = NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR;
    }
    return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
        MediaResult(err, "Remote process crashed"), __func__);
  }

  if (aSample->mType == MediaData::Type::AUDIO_DATA) {
    auto samples = MakeRefPtr<ArrayOfRemoteAudioData>();
    if (!samples->Fill(aSample->As<const AudioData>(), [&](size_t aSize) {
          return AllocateBuffer(aSize, aTicket);
        })) {
      LOGE("[%p] buffer audio failed", this);
      return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
          MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
    }
    LOGD("[%p] send audio", this);
    return SendEncode(std::move(samples));
  }

  if (aSample->mType == MediaData::Type::VIDEO_DATA) {
    auto samples = MakeRefPtr<ArrayOfRemoteVideoData>();
    const auto* videoSample = aSample->As<const VideoData>();
    if (layers::Image* videoImage = videoSample->mImage) {
      // We don't need to supply a working deallocator because the ticket is
      // responsible for that cleanup.
      layers::SurfaceDescriptor sd;
      nsresult rv = videoImage->BuildSurfaceDescriptorGPUVideoOrBuffer(
          sd, layers::Image::BuildSdbFlags::Default,
          Some(GetVideoBridgeSourceFromRemoteMediaIn(mLocation)),
          [&](uint32_t aBufferSize) {
            ShmemBuffer buffer = AllocateBuffer(aBufferSize, aTicket);
            if (buffer.Valid()) {
              return layers::MemoryOrShmem(std::move(buffer.Get()));
            }
            return layers::MemoryOrShmem();
          },
          [&](layers::MemoryOrShmem&&) {});

      if (NS_WARN_IF(NS_FAILED(rv))) {
        LOGE("[%p] buffer video failed, code=0x%08x", this, rv);
        return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
            MediaResult(rv), __func__);
      }

      samples->Append(RemoteVideoData(
          MediaDataIPDL(videoSample->mOffset, videoSample->mTime,
                        videoSample->mTimecode, videoSample->mDuration,
                        videoSample->mKeyframe),
          videoSample->mDisplay, RemoteImageHolder(std::move(sd)),
          videoSample->mFrameID));
    }
    LOGD("[%p] send video", this);
    return SendEncode(std::move(samples));
  }

  return PRemoteEncoderChild::EncodePromise::CreateAndResolve(
      MediaResult(NS_ERROR_INVALID_ARG), __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoderChild::Encode(
    const MediaData* aSample) {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this},
       sample = RefPtr{aSample}]() -> RefPtr<MediaDataEncoder::EncodePromise> {
        auto promise =
            MakeRefPtr<MediaDataEncoder::EncodePromise::Private>(__func__);
        auto ticket = MakeRefPtr<ShmemRecycleTicket>();
        self->DoSendEncode(sample, ticket)
            ->Then(
                self->mThread, __func__,
                [self, promise, ticket](EncodeResultIPDL&& aResponse) {
                  self->ReleaseTicket(ticket);

                  if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                    LOGE("[%p] Encode resolved, code=0x%08x", self.get(),
                         aResponse.get_MediaResult().Code());
                    promise->Reject(aResponse.get_MediaResult(), __func__);
                    return;
                  }

                  const auto& encodeResponse =
                      aResponse.get_EncodeCompletionIPDL();

                  nsTArray<RefPtr<MediaRawData>> samples;
                  if (auto remoteSamples = encodeResponse.samples()) {
                    size_t count = remoteSamples->Count();
                    samples.SetCapacity(count);
                    for (size_t i = 0; i < count; ++i) {
                      if (RefPtr<MediaRawData> sample =
                              remoteSamples->ElementAt(i)) {
                        samples.AppendElement(std::move(sample));
                      } else {
                        LOGE("[%p] Encode resolved, failed to buffer samples",
                             self.get());
                        promise->Reject(MediaResult(NS_ERROR_OUT_OF_MEMORY),
                                        __func__);
                        return;
                      }
                    }
                  }

                  LOGD("[%p] Encode resolved, %zu samples", self.get(),
                       samples.Length());
                  promise->Resolve(std::move(samples), __func__);
                  self->SendReleaseTicket(encodeResponse.ticketId());
                },
                [self, promise,
                 ticket](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[%p] Encode ipc failed", self.get());
                  self->ReleaseTicket(ticket);
                  self->GetManager()->HandleRejectionError(
                      aReason, [promise](const MediaResult& aError) {
                        promise->Reject(aError, __func__);
                      });
                });
        return promise;
      });
}

RefPtr<MediaDataEncoder::EncodePromise> RemoteMediaDataEncoderChild::Drain() {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<MediaDataEncoder::EncodePromise> {
        LOGD("[%p] Drain send", self.get());
        self->SendDrain()->Then(
            self->mThread, __func__,
            [self](EncodeResultIPDL&& aResponse) {
              if (aResponse.type() == EncodeResultIPDL::TMediaResult) {
                LOGE("[%p] Drain resolved, code=0x%08x", self.get(),
                     aResponse.get_MediaResult().Code());
                self->mDrainPromise.Reject(aResponse.get_MediaResult(),
                                           __func__);
                return;
              }

              const auto& encodeResponse = aResponse.get_EncodeCompletionIPDL();

              nsTArray<RefPtr<MediaRawData>> samples;
              if (auto remoteSamples = encodeResponse.samples()) {
                size_t count = remoteSamples->Count();
                samples.SetCapacity(count);
                for (size_t i = 0; i < count; ++i) {
                  if (RefPtr<MediaRawData> sample =
                          remoteSamples->ElementAt(i)) {
                    samples.AppendElement(std::move(sample));
                  } else {
                    LOGE("[%p] Drain resolved, failed to buffer samples",
                         self.get());
                    self->mDrainPromise.Reject(
                        MediaResult(NS_ERROR_OUT_OF_MEMORY), __func__);
                    return;
                  }
                }
              }

              LOGD("[%p] Drain resolved, %zu samples", self.get(),
                   samples.Length());
              self->mDrainPromise.Resolve(std::move(samples), __func__);
              self->SendReleaseTicket(encodeResponse.ticketId());
            },
            [self](const mozilla::ipc::ResponseRejectReason& aReason) {
              LOGE("[%p] Drain ipc failed", self.get());
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
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}, changes = RefPtr{aConfigurationChanges}]()
          -> RefPtr<MediaDataEncoder::ReconfigurationPromise> {
        LOGD("[%p] Reconfigure send", self.get());
        self->SendReconfigure(
                const_cast<EncoderConfigurationChangeList*>(changes.get()))
            ->Then(
                self->mThread, __func__,
                [self](const MediaResult& aResult) {
                  if (NS_SUCCEEDED(aResult)) {
                    LOGD("[%p] Reconfigure resolved", self.get());
                    self->mReconfigurePromise.ResolveIfExists(true, __func__);
                  } else {
                    LOGD("[%p] Reconfigure resolved, code=0x%08x", self.get(),
                         aResult.Code());
                    self->mReconfigurePromise.RejectIfExists(aResult, __func__);
                  }
                },
                [self](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[%p] Reconfigure ipc failed", self.get());
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
  {
    MutexAutoLock lock(mMutex);
    mNeedsShutdown = false;
  }

  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}]() -> RefPtr<mozilla::ShutdownPromise> {
        LOGD("[%p] Shutdown send", self.get());
        self->SendShutdown()->Then(
            self->mThread, __func__,
            [self](PRemoteEncoderChild::ShutdownPromise::ResolveOrRejectValue&&
                       aValue) {
              LOGD("[%p] Shutdown resolved", self.get());
              if (self->CanSend()) {
                self->Send__delete__(self);
              }
              self->mShutdownPromise.Resolve(aValue.IsResolve(), __func__);
            });
        return self->mShutdownPromise.Ensure(__func__);
      });
}

RefPtr<GenericPromise> RemoteMediaDataEncoderChild::SetBitrate(
    uint32_t aBitsPerSec) {
  return InvokeAsync(
      mThread, __func__,
      [self = RefPtr{this}, aBitsPerSec]() -> RefPtr<GenericPromise> {
        auto promise = MakeRefPtr<GenericPromise::Private>(__func__);
        self->SendSetBitrate(aBitsPerSec)
            ->Then(
                self->mThread, __func__,
                [promise](const nsresult& aRv) {
                  if (NS_SUCCEEDED(aRv)) {
                    promise->Resolve(true, __func__);
                  } else {
                    promise->Reject(aRv, __func__);
                  }
                },
                [self,
                 promise](const mozilla::ipc::ResponseRejectReason& aReason) {
                  LOGE("[%p] SetBitrate ipc failed", self.get());
                  self->GetManager()->HandleRejectionError(
                      aReason, [promise](const MediaResult& aError) {
                        promise->Reject(aError.Code(), __func__);
                      });
                });
        return promise.forget();
      });
}

RemoteMediaManagerChild* RemoteMediaDataEncoderChild::GetManager() {
  if (!CanSend()) {
    return nullptr;
  }
  return static_cast<RemoteMediaManagerChild*>(Manager());
}

bool RemoteMediaDataEncoderChild::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  MutexAutoLock lock(mMutex);
  aFailureReason = mHardwareAcceleratedReason;
  return mIsHardwareAccelerated;
}

nsCString RemoteMediaDataEncoderChild::GetDescriptionName() const {
  MutexAutoLock lock(mMutex);
  return mDescription;
}

}  // namespace mozilla
