/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <proxygen/lib/utils/Time.h>

using namespace proxygen;

TEST(TimeTest, GetDateTimeStr) {
  ASSERT_FALSE(getDateTimeStr(getCurrentTime()).empty());

  SystemClock::time_point sys_tp{}; // epoch timepoint
  SteadyClock::time_point tp = SteadyClock::now() +
    std::chrono::duration_cast<SteadyClock::duration>(
      sys_tp - SystemClock::now());
  ASSERT_EQ("1970-01-01T00:00:00 +0000", getDateTimeStr(tp));
}
