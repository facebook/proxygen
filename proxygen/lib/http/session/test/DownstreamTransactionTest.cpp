/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/codec/test/MockHTTPCodec.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>
#include <proxygen/lib/test/TestAsyncTransport.h>
#include <folly/io/async/test/MockAsyncTransport.h>



using namespace proxygen;
using namespace testing;

using folly::IOBuf;
using folly::IOBufQueue;
using std::string;
using std::unique_ptr;
using std::vector;

class DownstreamTransactionTest : public testing::Test {
 public:
  DownstreamTransactionTest() {}

  void SetUp() override {
    EXPECT_CALL(transport_, describe(_))
      .WillRepeatedly(Return());
  }

  void setupRequestResponseFlow(HTTPTransaction* txn, uint32_t size) {
    EXPECT_CALL(handler_, setTransaction(txn));
    EXPECT_CALL(handler_, detachTransaction());
    EXPECT_CALL(transport_, detach(txn));
    EXPECT_CALL(handler_, onHeadersComplete(_))
      .WillOnce(Invoke([=](std::shared_ptr<HTTPMessage> msg) {
            auto response = makeResponse(200);
            txn->sendHeaders(*response.get());
            txn->sendBody(makeBuf(size));
            txn->sendEOM();
          }));
    EXPECT_CALL(transport_, sendHeaders(txn, _, _, _))
      .WillOnce(Invoke([=](Unused, const HTTPMessage& headers, Unused, Unused) {
            EXPECT_EQ(headers.getStatusCode(), 200);
          }));
    EXPECT_CALL(transport_, sendBody(txn, _, false))
      .WillRepeatedly(Invoke([=](Unused, std::shared_ptr<folly::IOBuf> body,
                                 Unused) {
                               auto cur = body->computeChainDataLength();
                               sent_ += cur;
                               return cur;
                             }));
    EXPECT_CALL(transport_, sendEOM(txn))
      .WillOnce(InvokeWithoutArgs([=]() {
            CHECK_EQ(sent_, size);
            txn->onIngressBody(makeBuf(size), 0);
            txn->onIngressEOM();
            return 5;
          }));
    EXPECT_CALL(handler_, onBody(_))
      .WillRepeatedly(Invoke([=](std::shared_ptr<folly::IOBuf> body) {
            received_ += body->computeChainDataLength();
          }));
    EXPECT_CALL(handler_, onEOM())
      .WillOnce(InvokeWithoutArgs([=] {
            CHECK_EQ(received_, size);
          }));
    EXPECT_CALL(transport_, notifyPendingEgress())
      .WillOnce(InvokeWithoutArgs([=] {
            txn->onWriteReady(size, 1);
          }))
      .WillOnce(DoDefault()); // The second call is for sending the eom

    txn->setHandler(&handler_);
  }

 protected:
  folly::EventBase eventBase_;
  folly::HHWheelTimer::UniquePtr transactionTimeouts_{
      folly::HHWheelTimer::newTimer(
          &eventBase_,
          std::chrono::milliseconds(folly::HHWheelTimer::DEFAULT_TICK_INTERVAL),
          folly::AsyncTimeout::InternalEnum::NORMAL,
          std::chrono::milliseconds(500))};
  MockHTTPTransactionTransport transport_;
  StrictMock<MockHTTPHandler> handler_;
  HTTP2PriorityQueue txnEgressQueue_;
  uint32_t received_{0};
  uint32_t sent_{0};
};

/**
  * Test that the the transaction properly forwards callbacks to the
  * handler and that it interacts with its transport as expected.
  */
TEST_F(DownstreamTransactionTest, simple_callback_forwarding) {
  // flow control is disabled
  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()));
  setupRequestResponseFlow(&txn, 100);

  txn.onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing that we're sending a window update for simple requests
 */
TEST_F(DownstreamTransactionTest, regular_window_update) {
  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()),
    nullptr,
    true, // flow control enabled
    400,
    spdy::kInitialWindow);
  uint32_t reqBodySize = 220;
  setupRequestResponseFlow(&txn, reqBodySize);

  // test that the window update is generated
  EXPECT_CALL(transport_, sendWindowUpdate(_, reqBodySize));

  // run the test
  txn.onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing window increase using window update; we're actually using this in
 * production to avoid bumping the window using the SETTINGS frame
 */
TEST_F(DownstreamTransactionTest, window_increase) {
  // set initial window size higher than per-stream window
  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()),
    nullptr,
    true, // flow control enabled
    spdy::kInitialWindow,
    spdy::kInitialWindow);
  uint32_t reqSize = 500;
  setupRequestResponseFlow(&txn, reqSize);

  // we expect the difference from the per stream window and the initial window,
  // together with the bytes sent in the request
  uint32_t perStreamWindow = spdy::kInitialWindow + 1024 * 1024;
  uint32_t expectedWindowUpdate =
    perStreamWindow - spdy::kInitialWindow;
  EXPECT_CALL(transport_, sendWindowUpdate(_, expectedWindowUpdate));

  // use a higher window
  txn.setReceiveWindow(perStreamWindow);

  txn.onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing that we're not sending window update when per-stream window size is
 * smaller than the initial window size
 */
TEST_F(DownstreamTransactionTest, window_decrease) {
  // set initial window size higher than per-stream window
  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()),
    nullptr,
    true, // flow control enabled
    spdy::kInitialWindow,
    spdy::kInitialWindow);
  setupRequestResponseFlow(&txn, 500);

  // in this case, there should be no window update, as we decrease the window
  // below the number of bytes we're sending
  EXPECT_CALL(transport_, sendWindowUpdate(_, _)).Times(0);

  // use a smaller window
  uint32_t perStreamWindow = spdy::kInitialWindow - 1000;
  txn.setReceiveWindow(perStreamWindow);

  txn.onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

TEST_F(DownstreamTransactionTest, parse_error_cbs) {
  // Test where the transaction gets on parse error and then a body
  // callback. This is possible because codecs are stateless between
  // frames.

  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()));

  HTTPException err(HTTPException::Direction::INGRESS, "test");
  err.setHttpStatusCode(400);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&txn));
  EXPECT_CALL(handler_, onError(_))
    .WillOnce(Invoke([] (const HTTPException& ex) {
          ASSERT_EQ(ex.getDirection(), HTTPException::Direction::INGRESS);
          ASSERT_EQ(std::string(ex.what()), "test");
        }));
  // onBody() is suppressed since ingress is complete after ingress onError()
  // onEOM() is suppressed since ingress is complete after ingress onError()
  EXPECT_CALL(transport_, sendAbort(_, _));
  EXPECT_CALL(handler_, detachTransaction());
  EXPECT_CALL(transport_, detach(&txn));

  txn.setHandler(&handler_);
  txn.onError(err);
  // Since the transaction is already closed for ingress, giving it
  // ingress body causes the transaction to be aborted and closed
  // immediately.
  txn.onIngressBody(makeBuf(10), 0);

  eventBase_.loop();
}

TEST_F(DownstreamTransactionTest, detach_from_notify) {
  unique_ptr<StrictMock<MockHTTPHandler>> handler(
    new StrictMock<MockHTTPHandler>);

  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()));

  InSequence dummy;

  EXPECT_CALL(*handler, setTransaction(&txn));
  EXPECT_CALL(*handler, onHeadersComplete(_))
    .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> msg) {
          auto response = makeResponse(200);
          txn.sendHeaders(*response.get());
          txn.sendBody(makeBuf(10));
        }));
  EXPECT_CALL(transport_, sendHeaders(&txn, _, _, _))
    .WillOnce(Invoke([&](Unused, const HTTPMessage& headers, Unused, Unused) {
          EXPECT_EQ(headers.getStatusCode(), 200);
        }));
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(10));
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(-10))
    .WillOnce(InvokeWithoutArgs([&] () {
          txn.setHandler(nullptr);
          handler.reset();
        }));
  EXPECT_CALL(transport_, detach(&txn));

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS, "test");

  txn.setHandler(handler.get());
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.onError(err);
}

TEST_F(DownstreamTransactionTest, deferred_egress) {
  EXPECT_CALL(transport_, describe(_))
    .WillRepeatedly(Return());
  EXPECT_CALL(transport_, notifyPendingEgress())
    .WillRepeatedly(Return());

  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()),
    nullptr, true, 10, 10);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&txn));
  EXPECT_CALL(handler_, onHeadersComplete(_))
    .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> msg) {
          auto response = makeResponse(200);
          txn.sendHeaders(*response.get());
          txn.sendBody(makeBuf(10));
          txn.sendBody(makeBuf(20));
        }));
  EXPECT_CALL(transport_, sendHeaders(&txn, _, _, _))
    .WillOnce(Invoke([&](Unused, const HTTPMessage& headers, Unused, Unused) {
          EXPECT_EQ(headers.getStatusCode(), 200);
        }));

  // when enqueued
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(10));
  EXPECT_CALL(handler_, onEgressPaused());
  // sendBody
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(20));

  txn.setHandler(&handler_);
  txn.onIngressHeadersComplete(makeGetRequest());

  // onWriteReady, send, then dequeue (SPDY window now full)
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(-20));

  EXPECT_EQ(txn.onWriteReady(20, 1), false);

  // enqueued after window update
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(20));

  txn.onIngressWindowUpdate(20);

  // Buffer released on error
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(-20));
  EXPECT_CALL(handler_, onError(_));
  EXPECT_CALL(handler_, detachTransaction());
  EXPECT_CALL(transport_, detach(&txn));

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS, "test");
  txn.onError(err);
}

TEST_F(DownstreamTransactionTest, internal_error) {
  unique_ptr<StrictMock<MockHTTPHandler>> handler(
    new StrictMock<MockHTTPHandler>);

  HTTPTransaction txn(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, WheelTimerInstance(transactionTimeouts_.get()));

  InSequence dummy;

  EXPECT_CALL(*handler, setTransaction(&txn));
  EXPECT_CALL(*handler, onHeadersComplete(_))
    .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> msg) {
          auto response = makeResponse(200);
          txn.sendHeaders(*response.get());
        }));
  EXPECT_CALL(transport_, sendHeaders(&txn, _, _, _))
    .WillOnce(Invoke([&](Unused, const HTTPMessage& headers, Unused, Unused) {
          EXPECT_EQ(headers.getStatusCode(), 200);
        }));
  EXPECT_CALL(transport_, sendAbort(&txn, ErrorCode::INTERNAL_ERROR));
  EXPECT_CALL(*handler, detachTransaction());
  EXPECT_CALL(transport_, detach(&txn));

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS, "test");

  txn.setHandler(handler.get());
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.sendAbort();
}
