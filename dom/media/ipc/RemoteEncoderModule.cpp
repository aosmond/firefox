/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteEncoderModule.h"
#include "RemoteDecodeUtils.h"
#include "RemoteMediaDataEncoderChild.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

RemoteEncoderModule::RemoteEncoderModule(RemoteMediaIn aLocation)
    : mLocation(aLocation) {}

/* static */ already_AddRefed<PlatformEncoderModule>
RemoteEncoderModule::Create(RemoteMediaIn aLocation) {
  if (!XRE_IsContentProcess()) {
    // For now, the RemoteEncoderModule is only available in the content
    // process.
    MOZ_ASSERT_UNREACHABLE("Should not be created outside content process.");
    return nullptr;
  }
  return MakeAndAddRef<RemoteEncoderModule>(aLocation);
}

already_AddRefed<MediaDataEncoder> RemoteEncoderModule::CreateEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  nsCOMPtr<nsISerialEventTarget> thread =
      RemoteMediaManagerChild::GetManagerThread();
  if (!thread) {
    // Shutdown has begun.
    return nullptr;
  }

  auto encoder =
      MakeRefPtr<RemoteMediaDataEncoderChild>(std::move(thread), mLocation);

  // This returns a promise, but we know that once it returns, the only
  // interactions the caller can do will require a dispatch to the manager
  // thread. The necessary IPDL constructor events are already queued so the
  // order of events is preserved.
  RemoteMediaManagerChild::InitializeEncoder(RefPtr{encoder}, aConfig);

  return encoder.forget();
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
RemoteEncoderModule::AsyncCreateEncoder(const EncoderConfig& aEncoderConfig,
                                        const RefPtr<TaskQueue>& aTaskQueue) {
  nsCOMPtr<nsISerialEventTarget> thread =
      RemoteMediaManagerChild::GetManagerThread();
  if (!thread) {
    // Shutdown has begun.
    return PlatformEncoderModule::CreateEncoderPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_CANCELED,
                    "Remote manager not available"),
        __func__);
  }

  auto encoder =
      MakeRefPtr<RemoteMediaDataEncoderChild>(std::move(thread), mLocation);
  return RemoteMediaManagerChild::InitializeEncoder(std::move(encoder),
                                                    aEncoderConfig);
}

media::EncodeSupportSet RemoteEncoderModule::Supports(
    const EncoderConfig& aConfig) const {
  return SupportsCodec(aConfig.mCodec);
}

media::EncodeSupportSet RemoteEncoderModule::SupportsCodec(
    CodecType aCodecType) const {
  media::EncodeSupportSet supports =
      RemoteMediaManagerChild::Supports(mLocation, aCodecType);
  MOZ_LOG(sPEMLog, LogLevel::Debug,
          ("Sandbox %s encoder %s requested codec %d",
           RemoteMediaInToStr(mLocation),
           supports.isEmpty() ? "supports" : "rejects",
           static_cast<int>(aCodecType)));
  return supports;
}

}  // namespace mozilla
