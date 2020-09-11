/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/utils/Time.h>
#include <folly/portability/GTest.h>

using namespace proxygen;

TEST(TimeTest, GetDateTimeStr) {
  ASSERT_FALSE(getDateTimeStr(getCurrentTime()).empty());

  SystemClock::time_point sys_tp{}; // epoch timepoint
  SteadyClock::time_point tp =
      SteadyClock::now() + std::chrono::duration_cast<SteadyClock::duration>(
                               sys_tp - SystemClock::now());
  ASSERT_EQ("1970-01-01T00:00:00 +0000", getDateTimeStr(tp));
}
