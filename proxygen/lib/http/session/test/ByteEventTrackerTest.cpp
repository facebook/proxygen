/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <folly/portability/GMock.h>

#include <proxygen/lib/http/session/ByteEventTracker.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>

#include <chrono>

using namespace testing;
using namespace proxygen;

class MockByteEventTrackerCallback:
    public ByteEventTracker::Callback {
 public:
  GMOCK_METHOD1_(, noexcept,, onPingReplyLatency, void(int64_t));
  GMOCK_METHOD3_(, noexcept,, onLastByteEvent, void(HTTPTransaction*,
                                                    uint64_t, bool));
  GMOCK_METHOD0_(, noexcept,, onDeleteAckEvent, void());
};

class ByteEventTrackerTest : public Test {
 public:

  void SetUp() override {
    txn_.setTransportCallback(&transportCallback_);
  }

 protected:
  folly::EventBase eventBase_;
  WheelTimerInstance transactionTimeouts_{std::chrono::milliseconds(500),
      &eventBase_};
  NiceMock<MockHTTPTransactionTransport> transport_;
  StrictMock<MockHTTPHandler> handler_;
  HTTP2PriorityQueue txnEgressQueue_;
  HTTPTransaction txn_{
    TransportDirection::DOWNSTREAM,
      HTTPCodec::StreamID(1), 1, transport_,
      txnEgressQueue_, transactionTimeouts_.getWheelTimer(),
      transactionTimeouts_.getDefaultTimeout()};
  MockHTTPTransactionTransportCallback transportCallback_;
  MockByteEventTrackerCallback callback_;
  std::shared_ptr<ByteEventTracker> byteEventTracker_{
    new ByteEventTracker(&callback_)};
};

TEST_F(ByteEventTrackerTest, Ping) {
  byteEventTracker_->addPingByteEvent(10, proxygen::getCurrentTime(), 0);
  EXPECT_CALL(callback_, onPingReplyLatency(_));
  byteEventTracker_->processByteEvents(byteEventTracker_, 10);
}

TEST_F(ByteEventTrackerTest, Ttlb) {
  byteEventTracker_->addLastByteEvent(&txn_, 10);
  EXPECT_CALL(transportCallback_, headerBytesGenerated(_)); // sendAbort calls?
  txn_.sendAbort(); // put it in a state for detach
  EXPECT_CALL(transportCallback_, lastByteFlushed());
  EXPECT_CALL(callback_, onLastByteEvent(_, _, _));
  EXPECT_CALL(transport_, detach(_));
  byteEventTracker_->processByteEvents(byteEventTracker_, 10);
}
