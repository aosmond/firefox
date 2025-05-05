/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoderParent.h"
#include "RemoteMediaManagerParent.h"

namespace mozilla {

RemoteMediaDataEncoderParent::RemoteMediaDataEncoderParent(
    RemoteMediaManagerParent* aParent, nsISerialEventTarget* aManagerThread,
    TaskQueue* aEncodeTaskQueue)
    : ShmemRecycleAllocator(this) {}

RemoteMediaDataEncoderParent::~RemoteMediaDataEncoderParent() = default;

void RemoteMediaDataEncoderParent::Destroy() {}

IPCResult RemoteMediaDataEncoderParent::RecvInit(InitResolver&& aResolver) {
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvEncode(
    const EncodedInputIPDL& aData, EncodeResolver&& aResolver) {
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvReconfigure(
    EncoderConfigurationChangeList* aConfigurationChanges,
    ReconfigureResolver&& aResolver) {
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvDrain(DrainResolver&& aResolver) {
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvShutdown(
    ShutdownResolver&& aResolver) {
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvSetBitrate(
    const uint32_t& aBitrate) {
  return IPC_OK();
}

void RemoteMediaDataEncoderParent::ActorDestroy(ActorDestroyReason aWhy) {
}

bool RemoteMediaDataEncoderParent::OnManagerThread() { return false; }

}  // namespace mozilla
