/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include "proxygen/lib/http/session/HQStreamLookup.h"

using namespace proxygen;
using namespace testing;

namespace {
constexpr hq::PushId kPushIdOne = 1000;
constexpr hq::PushId kPushIdTwo = 1002;
constexpr quic::StreamId kStreamIdOne = 101;
constexpr quic::StreamId kStreamIdTwo = 103;
} // namespace

/**
 * Test case to validate that the stream lookup functionality is working
 * the way should work.
 */
class HQStreamLookupTest : public Test {
 public:
  void SetUp() override {
  }

  void TearDown() override {
  }

  void populateLookup() {
    streamLookup_.push_back(
        PushToStreamMap::value_type(kPushIdOne, kStreamIdOne));
    streamLookup_.push_back(
        PushToStreamMap::value_type(kPushIdTwo, kStreamIdTwo));
  }

 protected:
  PushToStreamMap streamLookup_;
};

// Test that the mocked `newTransaction` does the right thing
TEST_F(HQStreamLookupTest, addMapping) {
  streamLookup_.push_back(
      PushToStreamMap::value_type(kPushIdOne, kStreamIdOne));
  ASSERT_EQ(streamLookup_.size(), 1);
  streamLookup_.push_back(
      PushToStreamMap::value_type(kPushIdTwo, kStreamIdTwo));
  ASSERT_EQ(streamLookup_.size(), 2);
}

// Test that populate request works
TEST_F(HQStreamLookupTest, populateLookup) {
  populateLookup();
  ASSERT_EQ(streamLookup_.size(), 2);
}

// Test that lookup by push id works
TEST_F(HQStreamLookupTest, lookupByPushId) {
  populateLookup();
  auto res = streamLookup_.by<push_id>().find(kPushIdOne);
  ASSERT_NE(res, streamLookup_.by<push_id>().end());
  ASSERT_EQ(res->get<quic_stream_id>(), kStreamIdOne);
}

// Test that lookup by stream id works
TEST_F(HQStreamLookupTest, lookupByStreamId) {
  populateLookup();
  auto res = streamLookup_.by<quic_stream_id>().find(kStreamIdOne);
  ASSERT_NE(res, streamLookup_.by<quic_stream_id>().end());
  ASSERT_EQ(res->get<push_id>(), kPushIdOne);
}

// Test that lookup by stream id followed by erase works
TEST_F(HQStreamLookupTest, lookupByStreamIdAndErase) {
  populateLookup();
  auto res = streamLookup_.by<quic_stream_id>().find(kStreamIdOne);
  ASSERT_NE(res, streamLookup_.by<quic_stream_id>().end());

  streamLookup_.by<quic_stream_id>().erase(res);

  ASSERT_EQ(streamLookup_.by<quic_stream_id>().find(kStreamIdOne),
            streamLookup_.by<quic_stream_id>().end());

  auto res2 = streamLookup_.by<push_id>().find(kPushIdOne);
  ASSERT_EQ(res2, streamLookup_.by<push_id>().end());
  ASSERT_EQ(streamLookup_.by<quic_stream_id>().size(), 1);
  ASSERT_EQ(streamLookup_.by<push_id>().size(), 1);
  ASSERT_EQ(streamLookup_.size(), 1);
}

// Test that lookup by stream id followed by erase works
TEST_F(HQStreamLookupTest, cacheLookupAndErase) {
  populateLookup();
  auto lookup = streamLookup_.by<quic_stream_id>();
  auto res = lookup.find(kStreamIdOne);
  ASSERT_NE(res, lookup.end());

  // Erase via cache lookup view
  lookup.erase(res);

  // Check that underlying bimap does not have the item
  ASSERT_EQ(streamLookup_.by<quic_stream_id>().find(kStreamIdOne),
            streamLookup_.by<quic_stream_id>().end());

  // Check that the item is fully erased
  auto res2 = streamLookup_.by<push_id>().find(kPushIdOne);
  ASSERT_EQ(res2, streamLookup_.by<push_id>().end());
  ASSERT_EQ(streamLookup_.by<quic_stream_id>().size(), 1);
  ASSERT_EQ(streamLookup_.by<push_id>().size(), 1);
  ASSERT_EQ(streamLookup_.size(), 1);
}

// Symmetrical opposite of the previous test
TEST_F(HQStreamLookupTest, lookupByPushIdAndErase) {
  populateLookup();
  auto res = streamLookup_.by<push_id>().find(kPushIdOne);
  ASSERT_NE(res, streamLookup_.by<push_id>().end());

  streamLookup_.by<push_id>().erase(res);

  ASSERT_EQ(streamLookup_.by<push_id>().find(kPushIdOne),
            streamLookup_.by<push_id>().end());

  auto res2 = streamLookup_.by<quic_stream_id>().find(kStreamIdOne);
  ASSERT_EQ(res2, streamLookup_.by<quic_stream_id>().end());
  ASSERT_EQ(streamLookup_.by<quic_stream_id>().size(), 1);
  ASSERT_EQ(streamLookup_.by<push_id>().size(), 1);
  ASSERT_EQ(streamLookup_.size(), 1);
}
