/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_GPUVIDEOTEXTUREHOST_H
#define MOZILLA_GFX_GPUVIDEOTEXTUREHOST_H

#include "mozilla/layers/TextureHost.h"

namespace mozilla {
namespace layers {

class GPUVideoTextureHost : public TextureHost {
 public:
  static GPUVideoTextureHost* CreateFromDescriptor(
      const dom::ContentParentId& aContentId, TextureFlags aFlags,
      const SurfaceDescriptorGPUVideo& aDescriptor);

  virtual ~GPUVideoTextureHost();

  void DeallocateDeviceData() override {}

  gfx::SurfaceFormat GetFormat() const override;

  already_AddRefed<gfx::DataSourceSurface> GetAsSurface(
      gfx::DataSourceSurface* aSurface) override {
    return nullptr;  // XXX - implement this (for MOZ_DUMP_PAINTING)
  }

  gfx::YUVColorSpace GetYUVColorSpace() const override;
  gfx::ColorDepth GetColorDepth() const override;
  gfx::ColorRange GetColorRange() const override;

  gfx::IntSize GetSize() const override;

  bool IsValid() override;

#ifdef MOZ_LAYERS_HAVE_LOG
  const char* Name() override { return "GPUVideoTextureHost"; }
#endif

  void CreateRenderTexture(
      const wr::ExternalImageId& aExternalImageId) override;

  void MaybeDestroyRenderTexture() override;

  uint32_t NumSubTextures() override;

  void PushResourceUpdates(wr::TransactionBuilder& aResources,
                           ResourceUpdateOp aOp,
                           const Range<wr::ImageKey>& aImageKeys,
                           const wr::ExternalImageId& aExtID) override;

  void PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                        const wr::LayoutRect& aBounds,
                        const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
                        const Range<wr::ImageKey>& aImageKeys,
                        PushDisplayItemFlagSet aFlags) override;

  bool SupportsExternalCompositing(WebRenderBackend aBackend) override;

  void UnbindTextureSource() override;

  void NotifyNotUsed() override;

  BufferTextureHost* AsBufferTextureHost() override;

  DXGITextureHostD3D11* AsDXGITextureHostD3D11() override;

  DXGIYCbCrTextureHostD3D11* AsDXGIYCbCrTextureHostD3D11() override;

  bool IsWrappingSurfaceTextureHost() override;

  TextureHostType GetTextureHostType() override;

  bool NeedsDeferredDeletion() const override;

  const dom::ContentParentId& GetContentId() const { return mContentId; }

 protected:
  GPUVideoTextureHost(const dom::ContentParentId& aContentId,
                      TextureFlags aFlags,
                      const SurfaceDescriptorGPUVideo& aDescriptor);

  TextureHost* EnsureWrappedTextureHost();

  RefPtr<TextureHost> mWrappedTextureHost;
  dom::ContentParentId mContentId;
  SurfaceDescriptorGPUVideo mDescriptor;
};

}  // namespace layers
}  // namespace mozilla

#endif  // MOZILLA_GFX_GPUVIDEOTEXTUREHOST_H
