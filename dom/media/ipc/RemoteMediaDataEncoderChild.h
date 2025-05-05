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
#include "PlatformEncoderModule.h"

#define MEDIA_INLINE_DECL_THREADSAFE_REFCOUNTING_META(_class, _decl, _destroy, \
                                                      _last_ref, ...)          \
 public:                                                                       \
  _decl(MozExternalRefCountType) AddRef(void) __VA_ARGS__ {                    \
    MOZ_ASSERT_TYPE_OK_FOR_REFCOUNTING(_class)                                 \
    MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");                       \
    nsrefcnt count = ++mRefCnt;                                                \
    NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                        \
    return (nsrefcnt)count;                                                    \
  }                                                                            \
  _decl(MozExternalRefCountType) Release(void) __VA_ARGS__ {                   \
    MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");                           \
    nsrefcnt count = --mRefCnt;                                                \
    NS_LOG_RELEASE(this, count, #_class);                                      \
    if (count == 0) {                                                          \
      _destroy;                                                                \
      return 0;                                                                \
    }                                                                          \
    if (count == 1) {                                                          \
      _last_ref;                                                               \
    }                                                                          \
    return count;                                                              \
  }                                                                            \
  using HasThreadSafeRefCnt = std::true_type;                                  \
                                                                               \
 protected:                                                                    \
  ::mozilla::ThreadSafeAutoRefCnt mRefCnt;                                     \
                                                                               \
 public:

namespace mozilla {

class RemoteMediaManagerChild;

class RemoteMediaDataEncoderChild final
    : public ShmemRecycleAllocator<RemoteMediaDataEncoderChild>,
      public PRemoteEncoderChild,
      public MediaDataEncoder {
  friend class PRemoteEncoderChild;

 public:
  MEDIA_INLINE_DECL_THREADSAFE_REFCOUNTING_META(RemoteMediaDataEncoderChild,
                                                NS_IMETHOD_, delete(this),
                                                MaybeDestroyActor(), final);

  RemoteMediaDataEncoderChild(nsCOMPtr<nsISerialEventTarget>&& aThread,
                              RemoteMediaIn aLocation);

  nsISerialEventTarget* GetManagerThread() const { return mThread; }
  RemoteMediaIn GetLocation() const { return mLocation; }

  RefPtr<PRemoteEncoderChild::ConstructPromise> SendConstruct();
  void ActorDestroy(ActorDestroyReason aWhy) override;

  // MediaDataEncoder
  RefPtr<MediaDataEncoder::InitPromise> Init() override;
  RefPtr<MediaDataEncoder::EncodePromise> Encode(
      const MediaData* aSample) override;
  RefPtr<MediaDataEncoder::EncodePromise> Drain() override;
  RefPtr<MediaDataEncoder::ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<mozilla::ShutdownPromise> Shutdown() override;
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override;
  nsCString GetDescriptionName() const override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;

 private:
  virtual ~RemoteMediaDataEncoderChild();
  RemoteMediaManagerChild* GetManager();

  virtual RefPtr<PRemoteEncoderChild::EncodePromise> DoSendEncode(
      const MediaData* aSample, ShmemRecycleTicket* aTicket);

  void MaybeDestroyActor();

  const nsCOMPtr<nsISerialEventTarget> mThread;
  const RemoteMediaIn mLocation;

  MozPromiseHolder<MediaDataEncoder::InitPromise> mInitPromise;
  MozPromiseHolder<MediaDataEncoder::EncodePromise> mDrainPromise;
  MozPromiseHolder<MediaDataEncoder::ReconfigurationPromise>
      mReconfigurePromise;
  MozPromiseHolder<mozilla::ShutdownPromise> mShutdownPromise;

  mutable Mutex mMutex{"RemoteMediaDataEncoderChild"};

  nsCString mHardwareAcceleratedReason MOZ_GUARDED_BY(mMutex);
  nsCString mDescription MOZ_GUARDED_BY(mMutex);
  bool mIsHardwareAccelerated MOZ_GUARDED_BY(mMutex) = false;
  bool mRemoteEncoderCrashed MOZ_GUARDED_BY(mMutex) = false;
  bool mHasActor MOZ_GUARDED_BY(mMutex) = false;
};

}  // namespace mozilla

#endif  // include_dom_media_ipc_RemoteEncoderChild_h
