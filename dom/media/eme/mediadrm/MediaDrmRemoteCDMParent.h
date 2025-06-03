/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmRemoteCDMParent_h_
#define MediaDrmRemoteCDMParent_h_

#include "mozilla/Mutex.h"
#include "mozilla/RemoteCDMParent.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"

#include "media/NdkMediaCodec.h"
#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaDrm.h"
#include "media/NdkMediaError.h"

#include <map>

namespace mozilla {

// Requires API level 21+.
using AMediaCryptoFnPtr_isCryptoSchemeSupported = bool (*)(const AMediaUUID);
using AMediaCryptoFnPtr_delete = void (*)(AMediaCrypto*);
using AMediaCryptoFnPtr_new = AMediaCrypto* (*)(const AMediaUUID, const void*,
                                                size_t);

using AMediaDrmFnPtr_createByUUID = AMediaDrm* (*)(const uint8_t*);
using AMediaDrmFnPtr_openSession = media_status_t (*)(AMediaDrm*,
                                                      AMediaDrmSessionId*);
using AMediaDrmFnPtr_closeSession =
    media_status_t (*)(AMediaDrm*, const AMediaDrmSessionId*);
using AMediaDrmFnPtr_getProvisionRequest = media_status_t (*)(AMediaDrm*,
                                                              const uint8_t**,
                                                              size_t*,
                                                              const char**);
using AMediaDrmFnPtr_getKeyRequest = media_status_t (*)(
    AMediaDrm*, const AMediaDrmScope*, const uint8_t*, size_t, const char*,
    AMediaDrmKeyType, const AMediaDrmKeyValue*, size_t, const uint8_t**,
    size_t*);
using AMediaDrmFnPtr_getKeyRequestWithDefaultUrlAndType = media_status_t (*)(
    AMediaDrm*, const AMediaDrmScope*, const uint8_t*, size_t, const char*,
    AMediaDrmKeyType, const AMediaDrmKeyValue*, size_t, const uint8_t**,
    size_t*, const char**, AMediaDrmKeyRequestType* keyRequestType);
using AMediaDrmFnPtr_provideProvisionResponse =
    media_status_t (*)(AMediaDrm*, const uint8_t*, size_t);
using AMediaDrmFnPtr_provideKeyResponse =
    media_status_t (*)(AMediaDrm*, const AMediaDrmScope*, const uint8_t*,
                       size_t, AMediaDrmKeySetId*);
using AMediaDrmFnPtr_setOnEventListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmEventListener);
using AMediaDrmFnPtr_setPropertyByteArray = media_status_t (*)(AMediaDrm*,
                                                               const char*,
                                                               const uint8_t*,
                                                               size_t);
using AMediaDrmFnPtr_setPropertyString = media_status_t (*)(AMediaDrm*,
                                                            const char*,
                                                            const char*);
using AMediaDrmFnPtr_release = void (*)(AMediaDrm*);

using AMediaCodecCryptoInfoFnPtr_new =
    AMediaCodecCryptoInfo* (*)(int, uint8_t[16], uint8_t[16], cryptoinfo_mode_t,
                               size_t*, size_t*);

using AMediaCodecCryptoInfoFnPtr_delete =
    media_status_t (*)(AMediaCodecCryptoInfo*);

using AMediaCodecCryptoInfoFnPtr_setPattern = void (*)(AMediaCodecCryptoInfo*,
                                                       cryptoinfo_pattern_t*);

// Requires API level 29+, but can be built with NDKs supported API level 21+.
using AMediaDrmFnPtr_ExpirationUpdateListener =
    void (*)(AMediaDrm*, const AMediaDrmSessionId*, int64_t);
using AMediaDrmFnPtr_KeysChangeListener = void (*)(AMediaDrm*,
                                                   const AMediaDrmSessionId*,
                                                   const AMediaDrmKeyStatus*,
                                                   size_t, bool);
using AMediaDrmFnPtr_setOnExpirationUpdateListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmFnPtr_ExpirationUpdateListener);
using AMediaDrmFnPtr_setOnKeysChangeListener =
    media_status_t (*)(AMediaDrm*, AMediaDrmFnPtr_KeysChangeListener);

class MediaDrmCrypto final {
  template <typename T, typename... Args>
  friend RefPtr<T> MakeRefPtr(Args&&...);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmCrypto);

 public:
  AMediaCrypto* GetNdkCrypto() const { return mCrypto; }

 private:
  explicit MediaDrmCrypto(AMediaCrypto* aCrypto) : mCrypto(aCrypto) {}

  ~MediaDrmCrypto();

  AMediaCrypto* mCrypto;
};

class MediaDrmCryptoInfo final {
  template <typename T, typename... Args>
  friend already_AddRefed<T> MakeAndAddRef(Args&&...);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmCryptoInfo);

 public:
  AMediaCodecCryptoInfo* GetNdkCryptoInfo() const { return mCryptoInfo; }

 private:
  explicit MediaDrmCryptoInfo(AMediaCodecCryptoInfo* aCryptoInfo)
      : mCryptoInfo(aCryptoInfo) {}

  ~MediaDrmCryptoInfo();

  AMediaCodecCryptoInfo* mCryptoInfo;
};

class MediaDrmRemoteCDMParent final : public RemoteCDMParent {
  friend class MediaDrmCrypto;
  friend class MediaDrmCryptoInfo;

 public:
  explicit MediaDrmRemoteCDMParent(const nsAString& aKeySystem);

  already_AddRefed<MediaDrmCrypto> GetCrypto() const {
    MutexAutoLock lock(mMutex);
    return do_AddRef(mCrypto);
  }

  bool HasCrypto() const {
    MutexAutoLock lock(mMutex);
    return !!mCrypto;
  }

  already_AddRefed<MediaDrmCryptoInfo> CreateCryptoInfo(MediaRawData* aSample);

  // PRemoteCDMParent
  mozilla::ipc::IPCResult RecvInit(const RemoteCDMInitRequestIPDL& aRequest,
                                   InitResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCreateSession(
      RemoteCDMCreateSessionRequestIPDL&& aRequest,
      CreateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvLoadSession(
      const RemoteCDMLoadSessionRequestIPDL& aRequest,
      LoadSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvUpdateSession(
      const RemoteCDMUpdateSessionRequestIPDL& aRequest,
      UpdateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvRemoveSession(
      const nsString& aSessionId, RemoveSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCloseSession(
      const nsString& aSessionId, CloseSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvSetServerCertificate(
      mozilla::Span<uint8_t const> aCertificate,
      SetServerCertificateResolver&& aResolver) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  virtual ~MediaDrmRemoteCDMParent();

  static constexpr uint8_t CLEARKEY_UUID[] = {
      0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9,
      0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e};

  static constexpr uint8_t WIDEVINE_UUID[] = {
      0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
      0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

  static bool InitializeStatics();

  static void HandleEventCb(AMediaDrm* aMediaDrm,
                            const AMediaDrmSessionId* aSessionId,
                            AMediaDrmEventType aEventType, int aExtra,
                            const uint8_t* aData, size_t aDataSize);

  static void HandleExpirationUpdateCb(AMediaDrm* aDrm,
                                       const AMediaDrmSessionId* aSessionId,
                                       int64_t aExpiryTimeInMS);

  static void HandleKeysChangeCb(AMediaDrm* aDrm,
                                 const AMediaDrmSessionId* aSessionId,
                                 const AMediaDrmKeyStatus* aKeyStatus,
                                 size_t aNumKeys, bool aHasNewUsableKey);

  void HandleEvent(nsString&& aSessionId, AMediaDrmEventType aEventType,
                   int aExtra, nsTArray<uint8_t>&& aData);

  void HandleExpirationUpdate(nsString&& aSessionId, int aExpiryTimeInMS);

  void HandleKeysChange(nsString&& aSessionId, bool aHasNewUsableKey,
                        nsTArray<CDMKeyInfo>&& aKeyInfo);

  using InternalPromise =
      MozPromise<bool, MediaResult, /* IsExclusive */ false>;

  RefPtr<InternalPromise> EnsureHasAMediaCrypto();
  RefPtr<InternalPromise> EnsureProvisioned();

  void Destroy();

  struct Internals {
    explicit Internals(void* aLib) : mLib(aLib) {}
    ~Internals();

    std::map<AMediaDrm*, MediaDrmRemoteCDMParent*> cbMap;
    void* mLib = nullptr;

#define DEFINE_AMEDIA_SYMBOL(objName, fnName) \
  objName##FnPtr_##fnName m##objName##_##fnName = nullptr;
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, createByUUID);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, openSession);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, closeSession);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, getProvisionRequest);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, getKeyRequest);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, getKeyRequestWithDefaultUrlAndType);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, provideProvisionResponse);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, provideKeyResponse);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnEventListener);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setPropertyByteArray);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setPropertyString);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, release);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnExpirationUpdateListener);
    DEFINE_AMEDIA_SYMBOL(AMediaDrm, setOnKeysChangeListener);

    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, isCryptoSchemeSupported);
    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, delete);
    DEFINE_AMEDIA_SYMBOL(AMediaCrypto, new);

    DEFINE_AMEDIA_SYMBOL(AMediaCodecCryptoInfo, delete);
    DEFINE_AMEDIA_SYMBOL(AMediaCodecCryptoInfo, new);
    DEFINE_AMEDIA_SYMBOL(AMediaCodecCryptoInfo, setPattern);
#undef DEFINE_AMEDIA_SYMBOL
  };

  static StaticAutoPtr<Internals> sMediaNdk;

  struct SessionEntry {
    AMediaDrmSessionId id;
    nsAutoCString mimeType;
  };

  std::map<nsString, SessionEntry> mSessions;
  MozPromiseHolder<InternalPromise> mProvisionPromise;

  mutable Mutex mMutex;
  RefPtr<MediaDrmCrypto> mCrypto MOZ_GUARDED_BY(mMutex);

  const uint8_t* mUuid = nullptr;
  AMediaDrm* mDrm = nullptr;
  AMediaDrmSessionId mCryptoSessionId;

  Maybe<MediaResult> mCryptoError;
  Maybe<MediaResult> mProvisionError;
};

}  // namespace mozilla

#endif  // MediaDrmRemoteCDMParent_h_
