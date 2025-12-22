/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_windows_RemoteBackbuffer_h
#define widget_windows_RemoteBackbuffer_h

#include "nsIWidget.h"
#include "mozilla/widget/PCompositorWidgetParent.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/gfx/2D.h"
#include "nsTArray.h"
#include "prthread.h"
#include <windows.h>

namespace mozilla {
namespace widget {
namespace remote_backbuffer {

struct IpcRect;
struct SharedData;
struct BorrowResponseData;
struct PresentRequestData;
struct PresentResponseData;
class SharedImage;
class PresentableSharedImage;
class Provider;

class ProviderSharedThread {
 public:
  static bool Add(Provider* aProvider);

 private:
  ProviderSharedThread() = default;
  ~ProviderSharedThread();

  bool Start();
  void ThreadMain();

  static StaticAutoPtr<ProviderSharedThread> sInstance;

  Mutex mMutex{"remote_backbuffer::ProviderSharedThread"};
  nsTArray<Provider*> mPendingProviders MOZ_GUARDED_BY(mMutex);
  PRThread* mServiceThread = nullptr;
  HANDLE mControlEvent = INVALID_HANDLE_VALUE;
  bool mStopServiceThread = false;
};

class Provider {
 public:
  Provider();
  ~Provider();

  bool Initialize(HWND aWindowHandle, DWORD aTargetProcessId);

  Maybe<RemoteBackbufferHandles> CreateRemoteHandles();

  Provider(const Provider&) = delete;
  Provider(Provider&&) = delete;
  Provider& operator=(const Provider&) = delete;
  Provider& operator=(Provider&&) = delete;

 private:
  friend class ProviderSharedThread;

  void ThreadMain();
  void HandleRequest();
  void HandleBorrowRequest(BorrowResponseData* aResponseData,
                           bool aAllowSameBuffer);
  void HandlePresentRequest(const PresentRequestData& aRequestData,
                            PresentResponseData* aResponseData);

  HWND mWindowHandle;
  HANDLE mTargetProcess;
  HANDLE mFileMapping;
  HANDLE mRequestReadyEvent;
  HANDLE mResponseReadyEvent;
  SharedData* mSharedDataPtr;
  bool mStopServiceThread;
  PRThread* mServiceThread;
  std::unique_ptr<PresentableSharedImage> mBackbuffer;
};

class Client {
 public:
  Client();
  ~Client();

  bool Initialize(const RemoteBackbufferHandles& aRemoteHandles);

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget();
  bool PresentDrawTarget(gfx::IntRegion aDirtyRegion);

  Client(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(const Client&) = delete;
  Client& operator=(Client&&) = delete;

 private:
  HANDLE mFileMapping;
  HANDLE mRequestReadyEvent;
  HANDLE mResponseReadyEvent;
  SharedData* mSharedDataPtr;
  std::unique_ptr<SharedImage> mBackbuffer;
};

}  // namespace remote_backbuffer
}  // namespace widget
}  // namespace mozilla

#endif  // widget_windows_RemoteBackbuffer_h
