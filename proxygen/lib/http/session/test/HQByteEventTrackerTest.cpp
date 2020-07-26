/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <proxygen/lib/http/session/HQByteEventTracker.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>
#include <quic/api/test/MockQuicSocket.h>
#include <quic/api/test/Mocks.h>

#include <chrono>

using namespace testing;
using namespace proxygen;

using QuicByteEvent = quic::QuicSocket::ByteEvent;
using QuicByteEventType = quic::QuicSocket::ByteEvent::Type;

class HQByteEventTrackerTest : public Test {
 public:
  void SetUp() override {
    socket_ =
        std::make_shared<quic::MockQuicSocket>(nullptr, connectionCallback_);
    byteEventTracker_ =
        std::make_shared<HQByteEventTracker>(nullptr, socket_.get(), streamId_);
    txn_.setTransportCallback(&transportCallback_);
  }

  /**
   * Setup an EXPECT_CALL for QuicSocket::registerTxCallback.
   */
  void expectRegisterTxCallback(
      const uint64_t offset,
      quic::QuicSocket::ByteEventCallback** capturedCallbackPtr,
      folly::Expected<folly::Unit, quic::LocalErrorCode> returnVal =
          folly::Unit()) const {
    EXPECT_CALL(*socket_, registerTxCallback(streamId_, offset, _))
        .WillOnce(DoAll(SaveArg<2>(&*capturedCallbackPtr), Return(returnVal)));
  }

  /**
   * Get a matcher for a proxygen ByteEvent.
   */
  static auto getByteEventMatcher(ByteEvent::EventType eventType,
                                  uint64_t offset) {
    return AllOf(testing::Property(&ByteEvent::getType, eventType),
                 testing::Property(&ByteEvent::getByteOffset, offset));
  }

 protected:
  const HTTPCodec::StreamID streamId_{1};
  quic::MockConnectionCallback connectionCallback_;
  std::shared_ptr<quic::MockQuicSocket> socket_;
  folly::EventBase eventBase_;
  WheelTimerInstance transactionTimeouts_{std::chrono::milliseconds(500),
                                          &eventBase_};
  NiceMock<MockHTTPTransactionTransport> transport_;
  StrictMock<MockHTTPHandler> handler_;
  HTTP2PriorityQueue txnEgressQueue_;
  HTTPTransaction txn_{TransportDirection::DOWNSTREAM,
                       HTTPCodec::StreamID(1),
                       1,
                       transport_,
                       txnEgressQueue_,
                       transactionTimeouts_.getWheelTimer(),
                       transactionTimeouts_.getDefaultTimeout()};
  MockHTTPTransactionTransportCallback transportCallback_;
  std::shared_ptr<ByteEventTracker> byteEventTracker_;
};

/**
 * Test when first and last body byte are written in the same buffer
 */
TEST_F(HQByteEventTrackerTest, FirstLastBodyByteSingleWrite) {
  uint64_t firstBodyByteOffset = 1;
  uint64_t lastBodyByteOffset = 10;

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
  byteEventTracker_->addFirstBodyByteEvent(firstBodyByteOffset, &txn_);
  byteEventTracker_->addLastByteEvent(&txn_, lastBodyByteOffset);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, firstByteFlushed());
  quic::QuicSocket::ByteEventCallback* fbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(firstBodyByteOffset, &fbbTxQuicByteEventCallback);

  EXPECT_CALL(transportCallback_, lastByteFlushed());
  quic::QuicSocket::ByteEventCallback* lbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(lastBodyByteOffset, &lbbTxQuicByteEventCallback);

  byteEventTracker_->processByteEvents(byteEventTracker_, lastBodyByteOffset);
  ASSERT_THAT(fbbTxQuicByteEventCallback, NotNull());
  ASSERT_THAT(lbbTxQuicByteEventCallback, NotNull());
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::FIRST_BYTE, firstBodyByteOffset)));
  fbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = firstBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::LAST_BYTE, lastBodyByteOffset)));
  lbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = lastBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
}

/**
 * Test when first and last body byte are written in separate buffers
 */
TEST_F(HQByteEventTrackerTest, FirstLastBodyByteSeparateWrites) {
  uint64_t firstBodyByteOffset = 1;
  uint64_t lastBodyByteOffset = 10;
  uint64_t firstWriteBytes = 5; // bytes written on first write

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
  byteEventTracker_->addFirstBodyByteEvent(firstBodyByteOffset, &txn_);
  byteEventTracker_->addLastByteEvent(&txn_, lastBodyByteOffset);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, firstByteFlushed());
  quic::QuicSocket::ByteEventCallback* fbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(firstBodyByteOffset, &fbbTxQuicByteEventCallback);
  byteEventTracker_->processByteEvents(byteEventTracker_, firstWriteBytes);
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, lastByteFlushed());
  quic::QuicSocket::ByteEventCallback* lbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(lastBodyByteOffset, &lbbTxQuicByteEventCallback);
  byteEventTracker_->processByteEvents(byteEventTracker_, lastBodyByteOffset);
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  ASSERT_THAT(fbbTxQuicByteEventCallback, NotNull());
  ASSERT_THAT(lbbTxQuicByteEventCallback, NotNull());

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::FIRST_BYTE, firstBodyByteOffset)));
  fbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = firstBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::LAST_BYTE, lastBodyByteOffset)));
  lbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = lastBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
}

/**
 * Test when the first and last body byte have the same offset (single byte txn)
 */
TEST_F(HQByteEventTrackerTest, FirstLastBodyByteSingleByte) {
  uint64_t firstBodyByteOffset = 1;
  uint64_t lastBodyByteOffset = firstBodyByteOffset;

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
  byteEventTracker_->addFirstBodyByteEvent(firstBodyByteOffset, &txn_);
  byteEventTracker_->addLastByteEvent(&txn_, lastBodyByteOffset);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  InSequence s; // required due to same EXPECT_CALL triggered twice

  EXPECT_CALL(transportCallback_, firstByteFlushed());
  quic::QuicSocket::ByteEventCallback* fbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(firstBodyByteOffset, &fbbTxQuicByteEventCallback);

  EXPECT_CALL(transportCallback_, lastByteFlushed());
  quic::QuicSocket::ByteEventCallback* lbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(lastBodyByteOffset, &lbbTxQuicByteEventCallback);

  byteEventTracker_->processByteEvents(byteEventTracker_, lastBodyByteOffset);
  ASSERT_THAT(fbbTxQuicByteEventCallback, NotNull());
  ASSERT_THAT(lbbTxQuicByteEventCallback, NotNull());
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::FIRST_BYTE, firstBodyByteOffset)));
  fbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = firstBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_CALL(transportCallback_,
              trackedByteEventTX(getByteEventMatcher(
                  ByteEvent::EventType::LAST_BYTE, lastBodyByteOffset)));
  lbbTxQuicByteEventCallback->onByteEvent(
      QuicByteEvent{.id = streamId_,
                    .offset = lastBodyByteOffset,
                    .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
}

/**
 * Test when the QUIC byte events are canceled after registration
 */
TEST_F(HQByteEventTrackerTest, FirstLastBodyByteCancellation) {
  uint64_t firstBodyByteOffset = 1;
  uint64_t lastBodyByteOffset = 10;

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
  byteEventTracker_->addFirstBodyByteEvent(firstBodyByteOffset, &txn_);
  byteEventTracker_->addLastByteEvent(&txn_, lastBodyByteOffset);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, firstByteFlushed());
  quic::QuicSocket::ByteEventCallback* fbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(firstBodyByteOffset, &fbbTxQuicByteEventCallback);

  EXPECT_CALL(transportCallback_, lastByteFlushed());
  quic::QuicSocket::ByteEventCallback* lbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(lastBodyByteOffset, &lbbTxQuicByteEventCallback);

  byteEventTracker_->processByteEvents(byteEventTracker_, lastBodyByteOffset);
  ASSERT_THAT(fbbTxQuicByteEventCallback, NotNull());
  ASSERT_THAT(lbbTxQuicByteEventCallback, NotNull());
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, trackedByteEventTX(_)).Times(0);
  fbbTxQuicByteEventCallback->onByteEventCanceled(QuicByteEvent{
      .id = streamId_, .offset = 1, .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_CALL(transportCallback_, trackedByteEventTX(_)).Times(0);
  lbbTxQuicByteEventCallback->onByteEventCanceled(QuicByteEvent{
      .id = streamId_, .offset = 1, .type = QuicByteEventType::TX});
  Mock::VerifyAndClearExpectations(&transportCallback_);

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
}

/**
 * Test when registration of QUIC byte events fails
 */
TEST_F(HQByteEventTrackerTest, FirstLastBodyByteErrorOnRegistration) {
  uint64_t firstBodyByteOffset = 1;
  uint64_t lastBodyByteOffset = 10;

  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
  byteEventTracker_->addFirstBodyByteEvent(firstBodyByteOffset, &txn_);
  byteEventTracker_->addLastByteEvent(&txn_, lastBodyByteOffset);
  EXPECT_EQ(2, txn_.getNumPendingByteEvents());

  EXPECT_CALL(transportCallback_, firstByteFlushed());
  quic::QuicSocket::ByteEventCallback* fbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(firstBodyByteOffset,
                           &fbbTxQuicByteEventCallback,
                           folly::makeUnexpected(quic::LocalErrorCode()));

  EXPECT_CALL(transportCallback_, lastByteFlushed());
  quic::QuicSocket::ByteEventCallback* lbbTxQuicByteEventCallback = nullptr;
  expectRegisterTxCallback(lastBodyByteOffset,
                           &lbbTxQuicByteEventCallback,
                           folly::makeUnexpected(quic::LocalErrorCode()));

  byteEventTracker_->processByteEvents(byteEventTracker_, lastBodyByteOffset);
  Mock::VerifyAndClearExpectations(&socket_);
  Mock::VerifyAndClearExpectations(&transportCallback_);
  EXPECT_EQ(0, txn_.getNumPendingByteEvents());
}
