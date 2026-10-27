/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/ConstexprMath.h>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace proxygen::decompression {

inline uint64_t maxBytesForNextDecode(uint64_t freeBufferBytes,
                                      std::optional<uint64_t> maxIngressBytes,
                                      uint64_t bytesOutputSoFar) {
  if (!maxIngressBytes.has_value()) {
    return freeBufferBytes;
  }
  const auto remaining = folly::constexpr_sub_overflow_clamped<uint64_t>(
      *maxIngressBytes, bytesOutputSoFar);
  return std::min(
      freeBufferBytes,
      folly::constexpr_add_overflow_clamped<uint64_t>(remaining, 1));
}

inline bool exceedsMaxIngressBytes(uint64_t totalOutputBytes,
                                   std::optional<uint64_t> maxIngressBytes) {
  return maxIngressBytes.has_value() && totalOutputBytes > *maxIngressBytes;
}

// `bytesOutputSoFar` already includes `bytesJustDecoded`.
inline uint64_t bytesWithinMaxIngress(uint64_t bytesJustDecoded,
                                      std::optional<uint64_t> maxIngressBytes,
                                      uint64_t bytesOutputSoFar) {
  if (!exceedsMaxIngressBytes(bytesOutputSoFar, maxIngressBytes)) {
    return bytesJustDecoded;
  }
  return folly::constexpr_sub_overflow_clamped<uint64_t>(
      bytesJustDecoded, bytesOutputSoFar - *maxIngressBytes);
}

inline bool exceedsMaxDecompressionRatio(
    uint64_t totalInputBytes,
    uint64_t totalOutputBytes,
    std::optional<uint64_t> maxDecompressionRatio) {
  return maxDecompressionRatio.has_value() && totalInputBytes > 0 &&
         totalOutputBytes / totalInputBytes > *maxDecompressionRatio;
}

} // namespace proxygen::decompression
