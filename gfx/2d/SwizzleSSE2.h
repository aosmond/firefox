/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SWIZZLE_SSE2_H_
#define MOZILLA_GFX_SWIZZLE_SSE2_H_

#include <emmintrin.h>
#include <immintrin.h>
#include <tmmintrin.h>

#include "SwizzleGenericDecls.h"

namespace mozilla::gfx {

template <class Arch>
  requires std::same_as<Arch, xsimd::sse2> ||
           std::same_as<Arch, xsimd::ssse3> || std::same_as<Arch, xsimd::avx2>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> LoadRemainder_SIMD(
    const uint8_t* aSrc, size_t aLength) {
  __m128i px;
  __m128i px2;
  if constexpr (std::is_same_v<Arch, xsimd::avx2>) {
    if (aLength >= 4) {
      px2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aSrc));
      if (aLength == 4) {
        return _mm256_castsi128_si256(px2);
      }
      aSrc += 4 * 4;
      aLength -= 4;
    }
  }
  if (aLength >= 2) {
    // Load first 2 pixels
    px = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(aSrc));
    // Load third pixel
    if (aLength >= 3) {
      px = _mm_unpacklo_epi64(
          px,
          _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc + 2 * 4)));
    }
  } else {
    // Load single pixel
    px = _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc));
  }
  if constexpr (std::is_same_v<Arch, xsimd::avx2>) {
    return _mm256_set_m128i(px, px2);
  } else {
    return px;
  }
}

template <class Arch>
  requires std::same_as<Arch, xsimd::sse2> ||
           std::same_as<Arch, xsimd::ssse3> || std::same_as<Arch, xsimd::avx2>
static MOZ_ALWAYS_INLINE void StoreRemainder_SIMD(
    uint8_t* aDst, size_t aLength, const xsimd::batch<uint8_t, Arch>& aSrc) {
  __m128i px;
  if constexpr (std::is_same_v<Arch, xsimd::avx2>) {
    if (aLength >= 4) {
      _mm_storeu_si128(reinterpret_cast<__m128i*>(aDst),
                       _mm256_castsi256_si128(aSrc));
      if (aLength == 4) {
        return;
      }
      px = _mm256_extractf128_si256(aSrc, 1);
      aLength -= 4;
      aDst += 4 * 4;
    }
  } else {
    px = aSrc;
  }
  if (aLength >= 2) {
    // Store first 2 pixels
    _mm_storel_epi64(reinterpret_cast<__m128i*>(aDst), px);
    // Store third pixel
    if (aLength >= 3) {
      *reinterpret_cast<uint32_t*>(aDst + 2 * 4) =
          _mm_cvtsi128_si32(_mm_srli_si128(px, 2 * 4));
    }
  } else {
    // Store single pixel
    *reinterpret_cast<uint32_t*>(aDst) = _mm_cvtsi128_si32(px);
  }
}

template <class Arch>
  requires std::same_as<Arch, xsimd::sse2> || std::same_as<Arch, xsimd::ssse3>
static MOZ_ALWAYS_INLINE xsimd::batch<uint8_t, Arch> LoadRemainderRGB_SIMD(
    const uint8_t* aSrc, size_t aLength) {
  // Load aLength (1-4) packed RGB pixels (3-12 bytes) without reading past the
  // 3*aLength valid source bytes. Bytes beyond the loaded pixels are unused by
  // the expand swizzle and dropped by StoreRemainder_SIMD.
  if (aLength >= 2) {
    if (aLength >= 3) {
      // Bytes 0-7: pixels 0-1 (RGB) and pixel 2's RG.
      __m128i px = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(aSrc));
      if (aLength >= 4) {
        // Bytes 8-11: pixel 2's B and pixel 3 (RGB).
        return _mm_unpacklo_epi64(
            px,
            _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc + 8)));
      }
      // 3 pixels = 9 bytes; byte 8 is pixel 2's B.
      return _mm_insert_epi16(px, aSrc[8], 4);
    }
    // 2 pixels = 6 bytes: bytes 0-3 then bytes 4-5.
    return _mm_insert_epi16(
        _mm_cvtsi32_si128(*reinterpret_cast<const uint32_t*>(aSrc)),
        *reinterpret_cast<const uint16_t*>(aSrc + 4), 2);
  }
  // 1 pixel = 3 bytes: bytes 0-1 then byte 2.
  return _mm_cvtsi32_si128(*reinterpret_cast<const uint16_t*>(aSrc) |
                           (static_cast<uint32_t>(aSrc[2]) << 16));
}

template <class Arch>
  requires std::same_as<Arch, xsimd::sse2>
static MOZ_ALWAYS_INLINE xsimd::batch<uint16_t, Arch> ExtractAlpha_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint16_t, Arch>& aGreenAlpha) {
  return _mm_shufflehi_epi16(
      _mm_shufflelo_epi16(aGreenAlpha, _MM_SHUFFLE(3, 3, 1, 1)),
      _MM_SHUFFLE(3, 3, 1, 1));
}

template <class Arch>
  requires std::same_as<Arch, xsimd::sse2> || std::same_as<Arch, xsimd::ssse3>
static MOZ_ALWAYS_INLINE xsimd::batch<uint32_t, Arch> UnpremultiplyLookup_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint16_t, Arch>& aGa) {
  // Extract the alphas for the 4 pixels from the now isolated words.
  alignas(Arch::alignment())
      uint16_t alphaBuf[xsimd::batch<uint16_t, Arch>::size];
  aGa.store_aligned(alphaBuf);

  // Load the full 8.16 reciprocals from the table for each alpha and gather
  // them into 32-bit lanes Q0 Q1 Q2 Q3.
  __m128i q12 =
      _mm_unpacklo_epi32(_mm_cvtsi32_si128(sUnpremultiplyTable[alphaBuf[1]]),
                         _mm_cvtsi32_si128(sUnpremultiplyTable[alphaBuf[3]]));
  __m128i q34 =
      _mm_unpacklo_epi32(_mm_cvtsi32_si128(sUnpremultiplyTable[alphaBuf[5]]),
                         _mm_cvtsi32_si128(sUnpremultiplyTable[alphaBuf[7]]));
  return _mm_unpacklo_epi64(q12, q34);
}

template <class Arch>
  requires std::same_as<Arch, xsimd::avx2>
static MOZ_ALWAYS_INLINE xsimd::batch<uint32_t, Arch> UnpremultiplyLookup_SIMD(
    const xsimd::batch<uint8_t, Arch>& aSrc,
    const xsimd::batch<uint16_t, Arch>& aGa) {
  // Extract the alphas (the odd 16-bit lanes of ga) for the 8 pixels. We do a
  // plain scalar gather through aligned buffers rather than vpgatherdd, whose
  // throughput is unreliable across the CPUs we target; the compiler folds this
  // to vpextr/vpinsr with no stack spill.
  alignas(Arch::alignment())
      uint16_t alphaBuf[xsimd::batch<uint16_t, Arch>::size];
  aGa.store_aligned(alphaBuf);

  // Load the full 8.16 reciprocal from the table for each alpha, gathered into
  // 32-bit lanes Q0..Q7.
  alignas(Arch::alignment())
      uint32_t recipBuf[xsimd::batch<uint32_t, Arch>::size];
  for (size_t i = 0; i < xsimd::batch<uint32_t, Arch>::size; ++i) {
    recipBuf[i] = sUnpremultiplyTable[alphaBuf[2 * i + 1]];
  }
  return xsimd::batch<uint32_t, Arch>::load_aligned(recipBuf);
}

}  // namespace mozilla::gfx

#endif /* MOZILLA_GFX_SWIZZLE_SSE2_H_ */
