/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/UnframedBodyOffsetTracker.h>
#include <folly/portability/GTest.h>

using UnframedBodyOffsetTracker = proxygen::hq::UnframedBodyOffsetTracker;

TEST(UnframedBodyOffsetTrackerTest, TestStartBodyTracking) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());
}

TEST(UnframedBodyOffsetTrackerTest, TestStartBodyTrackingTwice) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());

  res = tracker.startBodyTracking(streamOffset + 5);
  EXPECT_TRUE(res.hasError());
}

TEST(UnframedBodyOffsetTrackerTest, TestBodyStarted) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  EXPECT_FALSE(tracker.bodyStarted());

  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_TRUE(tracker.bodyStarted());
}

TEST(UnframedBodyOffsetTrackerTest, TestAddBodyBytesProcessed) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());

  tracker.addBodyBytesProcessed(53);
  EXPECT_EQ(tracker.getBodyBytesProcessed(), 53);
}

TEST(UnframedBodyOffsetTrackerTest, TestMaybeMoveBodyBytesProcessed) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_TRUE(tracker.maybeMoveBodyBytesProcessed(17));
  EXPECT_EQ(tracker.getBodyBytesProcessed(), 17);
}

TEST(UnframedBodyOffsetTrackerTest, TestMaybeMoveBodyBytesProcessedFalse) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());

  tracker.addBodyBytesProcessed(53);
  EXPECT_EQ(tracker.getBodyBytesProcessed(), 53);
  EXPECT_FALSE(tracker.maybeMoveBodyBytesProcessed(17));
}

TEST(UnframedBodyOffsetTrackerTest, TestGetBodyStreamStartOffset) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());

  auto offset = tracker.getBodyStreamStartOffset();
  EXPECT_FALSE(offset.hasError());
}

TEST(UnframedBodyOffsetTrackerTest, TestGetBodyStreamStartOffsetError) {
  UnframedBodyOffsetTracker tracker{};
  auto offset = tracker.getBodyStreamStartOffset();
  EXPECT_TRUE(offset.hasError());
}

TEST(UnframedBodyOffsetTrackerTest, TestAppTostreamOffset) {
  const uint64_t streamOffset = 42;
  UnframedBodyOffsetTracker tracker{};
  auto res = tracker.startBodyTracking(streamOffset);
  EXPECT_FALSE(res.hasError());

  auto offset = tracker.appTostreamOffset(5674);
  EXPECT_FALSE(offset.hasError());
  EXPECT_EQ(*offset, 42 + 5674);
}

TEST(UnframedBodyOffsetTrackerTest, TestAppTostreamOffsetError) {
  UnframedBodyOffsetTracker tracker{};
  auto offset = tracker.appTostreamOffset(42);
  EXPECT_TRUE(offset.hasError());
}
