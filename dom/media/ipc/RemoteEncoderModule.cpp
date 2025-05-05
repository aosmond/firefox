/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteEncoderModule.h"

namespace mozilla {

RemoteEncoderModule::RemoteEncoderModule(RemoteMediaIn aLocation)
    : mLocation(aLocation) {}

already_AddRefed<MediaDataEncoder> RemoteEncoderModule::CreateVideoEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  return nullptr;
}

already_AddRefed<MediaDataEncoder> RemoteEncoderModule::CreateAudioEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  return nullptr;
}

bool RemoteEncoderModule::Supports(const EncoderConfig& aConfig) const {
  return false;
}

bool RemoteEncoderModule::SupportsCodec(CodecType aCodecType) const {
  return false;
}

}  // namespace mozilla
