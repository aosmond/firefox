/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(RemoteEncoderModule_h_)
#  define RemoteEncoderModule_h_

#  include "PlatformEncoderModule.h"
#  include "RemoteMediaManagerChild.h"

namespace mozilla {

class RemoteEncoderModule final : public PlatformEncoderModule {
 public:
  already_AddRefed<MediaDataEncoder> CreateVideoEncoder(
      const EncoderConfig& aConfig,
      const RefPtr<TaskQueue>& aTaskQueue) const override;

  already_AddRefed<MediaDataEncoder> CreateAudioEncoder(
      const EncoderConfig& aConfig,
      const RefPtr<TaskQueue>& aTaskQueue) const override;

  bool Supports(const EncoderConfig& aConfig) const override;
  bool SupportsCodec(CodecType aCodecType) const override;

  const char* GetName() const override { return "Remote Encoder Module"; }

 private:
  explicit RemoteEncoderModule(RemoteMediaIn aLocation);

  const RemoteMediaIn mLocation;
};

}  // namespace mozilla

#endif /* RemoteEncoderModule_h_ */
