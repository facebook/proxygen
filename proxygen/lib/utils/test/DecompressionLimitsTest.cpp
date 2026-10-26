/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/utils/DecompressionLimits.h>

#include <folly/portability/GTest.h>

#include <limits>

using proxygen::decompression::exceedsMaxDecompressionRatio;
using proxygen::decompression::maxBytesForNextDecode;

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeWithoutMaxIsFreeBuffer) {
  EXPECT_EQ(maxBytesForNextDecode(4096, std::nullopt, 1'000'000), 4096);
}

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeIsFreeBufferUnderMax) {
  EXPECT_EQ(maxBytesForNextDecode(4096, 10'000, 0), 4096);
  EXPECT_EQ(maxBytesForNextDecode(4096, 4096, 0), 4096);
}

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeIsOnePastBytesLeft) {
  EXPECT_EQ(maxBytesForNextDecode(4096, 100, 60), 41);
}

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeIsOneAtMax) {
  EXPECT_EQ(maxBytesForNextDecode(4096, 100, 100), 1);
  EXPECT_EQ(maxBytesForNextDecode(4096, 0, 0), 1);
}

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeAfterMaxIsOne) {
  EXPECT_EQ(maxBytesForNextDecode(4096, 100, 101), 1);
  EXPECT_EQ(maxBytesForNextDecode(4096, 0, 1), 1);
  EXPECT_EQ(
      maxBytesForNextDecode(4096, 0, std::numeric_limits<uint64_t>::max()), 1);
  EXPECT_EQ(maxBytesForNextDecode(0, 100, 101), 0);
}

TEST(DecompressionLimitsTest, MaxBytesForNextDecodeDoesNotOverflow) {
  const auto max = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(maxBytesForNextDecode(4096, max, 0), 4096);
  EXPECT_EQ(maxBytesForNextDecode(max, max, 0), max);
  EXPECT_EQ(maxBytesForNextDecode(max, max, 1), max);
  EXPECT_EQ(maxBytesForNextDecode(max, max, max), 1);
}

TEST(DecompressionLimitsTest, DecompressionRatioWithoutMaxIsNeverExceeded) {
  EXPECT_FALSE(exceedsMaxDecompressionRatio(
      1, std::numeric_limits<uint64_t>::max(), std::nullopt));
}

TEST(DecompressionLimitsTest, DecompressionRatioWithoutInputIsNotExceeded) {
  EXPECT_FALSE(exceedsMaxDecompressionRatio(0, 1'000'000, 10));
}

TEST(DecompressionLimitsTest, DecompressionRatioIsExceededOnlyAboveMax) {
  EXPECT_FALSE(exceedsMaxDecompressionRatio(100, 1'000, 10));
  EXPECT_FALSE(exceedsMaxDecompressionRatio(100, 1'099, 10));
  EXPECT_TRUE(exceedsMaxDecompressionRatio(100, 1'100, 10));
}
