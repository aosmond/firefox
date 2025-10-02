/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GPUProcessImpl.h"
#include "nsXPCOM.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/Logging.h"

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "nsAppShell.h"
#  include "mozilla/sandboxTarget.h"
#elif defined(__OpenBSD__) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#endif

namespace mozilla {
namespace gfx {

using namespace ipc;

static LazyLogModule sGPUProcessImplLog("GPUProcessImpl");

GPUProcessImpl::~GPUProcessImpl() = default;

bool GPUProcessImpl::Init(int aArgc, char* aArgv[]) {
  NS_WARNING("GPUProcessImpl::Init -- enter");
  MOZ_LOG_FMT(sGPUProcessImplLog, LogLevel::Debug, "Init: enter");

#if defined(MOZ_SANDBOX) && defined(XP_WIN)
  nsAppShell::PrecacheEventWindow();
  mozilla::SandboxTarget::Instance()->StartSandbox();
#elif defined(__OpenBSD__) && defined(MOZ_SANDBOX)
  StartOpenBSDSandbox(GeckoProcessType_GPU);
#endif

  Maybe<const char*> parentBuildID =
      geckoargs::sParentBuildID.Get(aArgc, aArgv);
  if (parentBuildID.isNothing()) {
    NS_WARNING("GPUProcessImpl::Init -- exit, bad build ID");
    MOZ_LOG_FMT(sGPUProcessImplLog, LogLevel::Debug, "Init: no parent build ID");
    return false;
  }

  if (!ProcessChild::InitPrefs(aArgc, aArgv)) {
    NS_WARNING("GPUProcessImpl::Init -- exit, bad prefs");
    MOZ_LOG_FMT(sGPUProcessImplLog, LogLevel::Debug, "Init: init prefs failed");
    return false;
  }

  NS_WARNING("GPUProcessImpl::Init -- init IPDL actor");
  MOZ_LOG_FMT(sGPUProcessImplLog, LogLevel::Debug, "Init: init IPDL actor");
  return mGPU->Init(TakeInitialEndpoint(), *parentBuildID);
  NS_WARNING("GPUProcessImpl::Init -- exit");
}

void GPUProcessImpl::CleanUp() { NS_ShutdownXPCOM(nullptr); }

}  // namespace gfx
}  // namespace mozilla
