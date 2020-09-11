/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/MockAsyncTransport.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/codec/test/MockHTTPCodec.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>
#include <proxygen/lib/test/TestAsyncTransport.h>

using namespace proxygen;
using namespace testing;

using std::unique_ptr;

class DownstreamTransactionTest : public testing::Test {
 public:
  DownstreamTransactionTest() {
  }

  void SetUp() override {
    EXPECT_CALL(transport_, describe(_)).WillRepeatedly(Return());
  }

  void setupRequestResponseFlow(HTTPTransaction* txn,
                                uint32_t size,
                                bool delayResponse = false) {
    EXPECT_CALL(handler_, setTransaction(txn));
    EXPECT_CALL(handler_, detachTransaction());
    EXPECT_CALL(transport_, detach(txn));
    if (delayResponse) {
      EXPECT_CALL(handler_, onHeadersComplete(_));
    } else {
      EXPECT_CALL(handler_, onHeadersComplete(_))
          .WillOnce(Invoke([=](std::shared_ptr<HTTPMessage> /*msg*/) {
            auto response = makeResponse(200);
            txn->sendHeaders(*response.get());
            txn->sendBody(makeBuf(size));
            txn->sendEOM();
          }));
    }
    EXPECT_CALL(transport_, sendHeaders(txn, _, _, _))
        .WillOnce(
            Invoke([=](Unused, const HTTPMessage& headers, Unused, Unused) {
              EXPECT_EQ(headers.getStatusCode(), 200);
            }));
    EXPECT_CALL(transport_, sendBody(txn, _, false, false))
        .WillRepeatedly(Invoke(
            [=](Unused, std::shared_ptr<folly::IOBuf> body, Unused, Unused) {
              auto cur = body->computeChainDataLength();
              sent_ += cur;
              return cur;
            }));
    if (delayResponse) {
      EXPECT_CALL(transport_, sendEOM(txn, _));
    } else {
      EXPECT_CALL(transport_, sendEOM(txn, _))
          .WillOnce(InvokeWithoutArgs([=]() {
            CHECK_EQ(sent_, size);
            txn->onIngressBody(makeBuf(size), 0);
            txn->onIngressEOM();
            return 5;
          }));
    }
    EXPECT_CALL(handler_, onBodyWithOffset(_, _))
        .WillRepeatedly(
            Invoke([=](uint64_t, std::shared_ptr<folly::IOBuf> body) {
              received_ += body->computeChainDataLength();
            }));
    EXPECT_CALL(handler_, onEOM()).WillOnce(InvokeWithoutArgs([=] {
      CHECK_EQ(received_, size);
    }));
    EXPECT_CALL(transport_, notifyPendingEgress())
        .WillOnce(InvokeWithoutArgs([=] { txn->onWriteReady(size, 1); }))
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
TEST_F(DownstreamTransactionTest, SimpleCallbackForwarding) {
  // flow control is disabled
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500));
  setupRequestResponseFlow(&txn, 100);

  txn.onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing that we're sending a window update for simple requests
 */
TEST_F(DownstreamTransactionTest, RegularWindowUpdate) {
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
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

TEST_F(DownstreamTransactionTest, NoWindowUpdate) {
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
                      nullptr,
                      true, // flow control enabled
                      450,  // more than 2x req size
                      spdy::kInitialWindow);
  uint32_t reqBodySize = 220;
  setupRequestResponseFlow(&txn, reqBodySize, true);

  EXPECT_CALL(transport_, sendWindowUpdate(_, reqBodySize)).Times(0);

  // run the test
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.onIngressBody(makeBuf(reqBodySize), 0);
  txn.onIngressEOM();
  auto response = makeResponse(200);
  txn.sendHeaders(*response.get());
  txn.sendBody(makeBuf(reqBodySize));
  txn.sendEOM();
  eventBase_.loop();
}

TEST_F(DownstreamTransactionTest, FlowControlInfoCorrect) {
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(157784760000),
                      nullptr,
                      true,
                      450,
                      100);

  EXPECT_CALL(transport_, getFlowControlInfo(_))
      .WillOnce(Invoke([=](HTTPTransaction::FlowControlInfo* info) {
        info->flowControlEnabled_ = true;
        info->sessionSendWindow_ = 1;
        info->sessionRecvWindow_ = 2;
      }));
  HTTPTransaction::FlowControlInfo info;
  txn.getCurrentFlowControlInfo(&info);

  EXPECT_EQ(info.flowControlEnabled_, true);
  EXPECT_EQ(info.sessionSendWindow_, 1);
  EXPECT_EQ(info.sessionRecvWindow_, 2);
  EXPECT_EQ(info.streamRecvWindow_, 450);
  EXPECT_EQ(info.streamSendWindow_, 100);
}

TEST_F(DownstreamTransactionTest, ExpectingWindowUpdate) {
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(157784760000),
                      nullptr,
                      true,
                      450,
                      100);
  uint32_t reqBodySize = 220;

  // Get a request, pause ingress, fill up the sendWindow, then expect for a
  // timeout to be scheduled.
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.pauseIngress();
  txn.onIngressBody(makeBuf(reqBodySize), 0);
  txn.onIngressEOM();
  auto response = makeResponse(200);
  txn.sendHeaders(*response.get());
  txn.sendBody(makeBuf(reqBodySize));
  txn.sendEOM();
  txn.onWriteReady(1000, 1);
  EXPECT_EQ(transactionTimeouts_->count(), 1);
}

TEST_F(DownstreamTransactionTest, NoWindowUpdateAfterDoneSending) {
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(157784760000),
                      nullptr,
                      true,
                      450,
                      220);
  uint32_t reqBodySize = 220;

  // Ensure that after flushing an EOM we are not expecting window update.
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.onIngressBody(makeBuf(reqBodySize), 0);
  auto response = makeResponse(200);
  txn.sendHeaders(*response.get());
  txn.sendBody(makeBuf(reqBodySize));
  txn.sendEOM();
  txn.onWriteReady(1000, 1);
  txn.pauseIngress();
  txn.onIngressEOM();
  EXPECT_EQ(transactionTimeouts_->count(), 0);
}

/**
 * Testing window increase using window update; we're actually using this in
 * production to avoid bumping the window using the SETTINGS frame
 */
TEST_F(DownstreamTransactionTest, WindowIncrease) {
  // set initial window size higher than per-stream window
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
                      nullptr,
                      true, // flow control enabled
                      spdy::kInitialWindow,
                      spdy::kInitialWindow);
  uint32_t reqSize = 500;
  setupRequestResponseFlow(&txn, reqSize);

  // we expect the difference from the per stream window and the initial window,
  // together with the bytes sent in the request
  uint32_t perStreamWindow = spdy::kInitialWindow + 1024 * 1024;
  uint32_t expectedWindowUpdate = perStreamWindow - spdy::kInitialWindow;
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
TEST_F(DownstreamTransactionTest, WindowDecrease) {
  // set initial window size higher than per-stream window
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
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

TEST_F(DownstreamTransactionTest, ParseErrorCbs) {
  // Test where the transaction gets on parse error and then a body
  // callback. This is possible because codecs are stateless between
  // frames.

  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500));

  HTTPException err(HTTPException::Direction::INGRESS, "test");
  err.setHttpStatusCode(400);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&txn));
  EXPECT_CALL(handler_, onError(_))
      .WillOnce(Invoke([](const HTTPException& ex) {
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

TEST_F(DownstreamTransactionTest, DetachFromNotify) {
  unique_ptr<StrictMock<MockHTTPHandler>> handler(
      new StrictMock<MockHTTPHandler>);

  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500));

  InSequence dummy;

  EXPECT_CALL(*handler, setTransaction(&txn));
  EXPECT_CALL(*handler, onHeadersComplete(_))
      .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> /*msg*/) {
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
      .WillOnce(InvokeWithoutArgs([&]() {
        txn.setHandler(nullptr);
        handler.reset();
      }));
  EXPECT_CALL(transport_, detach(&txn));

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS, "test");

  txn.setHandler(handler.get());
  txn.onIngressHeadersComplete(makeGetRequest());
  txn.onError(err);
}

TEST_F(DownstreamTransactionTest, DeferredEgress) {
  EXPECT_CALL(transport_, describe(_)).WillRepeatedly(Return());
  EXPECT_CALL(transport_, notifyPendingEgress()).WillRepeatedly(Return());

  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
                      nullptr,
                      true,
                      10,
                      10);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&txn));
  EXPECT_CALL(handler_, onHeadersComplete(_))
      .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> /*msg*/) {
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
  EXPECT_CALL(transport_, notifyEgressBodyBuffered(-10));
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

TEST_F(DownstreamTransactionTest, InternalError) {
  unique_ptr<StrictMock<MockHTTPHandler>> handler(
      new StrictMock<MockHTTPHandler>);

  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500));

  InSequence dummy;

  EXPECT_CALL(*handler, setTransaction(&txn));
  EXPECT_CALL(*handler, onHeadersComplete(_))
      .WillOnce(Invoke([&](std::shared_ptr<HTTPMessage> /*msg*/) {
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

TEST_F(DownstreamTransactionTest, UnpausedFlowControlViolation) {
  StrictMock<MockHTTPHandler> handler;

  InSequence enforceOrder;
  HTTPTransaction txn(TransportDirection::DOWNSTREAM,
                      HTTPCodec::StreamID(1),
                      1,
                      transport_,
                      txnEgressQueue_,
                      transactionTimeouts_.get(),
                      std::chrono::milliseconds(500),
                      nullptr,
                      true, // flow control enabled
                      400,
                      spdy::kInitialWindow);

  EXPECT_CALL(handler, setTransaction(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_));
  EXPECT_CALL(transport_, sendAbort(&txn, ErrorCode::FLOW_CONTROL_ERROR));
  EXPECT_CALL(handler, detachTransaction());
  EXPECT_CALL(transport_, detach(&txn));

  txn.setHandler(&handler);
  txn.onIngressHeadersComplete(makePostRequest(401));
  txn.onIngressBody(makeBuf(401), 0);
}

TEST_F(DownstreamTransactionTest, ParseIngressErrorExTxnUnidirectional) {
  // Test where the ex transaction using QoS0 gets Ingress error
  HTTPTransaction exTxn(TransportDirection::DOWNSTREAM,
                        HTTPCodec::StreamID(2),
                        1,
                        transport_,
                        txnEgressQueue_,
                        transactionTimeouts_.get(),
                        std::chrono::milliseconds(500),
                        nullptr,
                        false,
                        0,
                        0,
                        http2::DefaultPriority,
                        HTTPCodec::NoStream,
                        HTTPCodec::ExAttributes(1, true));

  HTTPException err(HTTPException::Direction::INGRESS, "test");
  err.setHttpStatusCode(400);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&exTxn));
  EXPECT_CALL(handler_, onError(_))
      .WillOnce(Invoke([](const HTTPException& ex) {
        ASSERT_EQ(ex.getDirection(), HTTPException::Direction::INGRESS);
        ASSERT_EQ(std::string(ex.what()), "test");
      }));
  // onBody() is suppressed since ingress is complete after ingress onError()
  // onEOM() is suppressed since ingress is complete after ingress onError()
  EXPECT_CALL(transport_, sendAbort(_, _));
  EXPECT_CALL(handler_, detachTransaction());
  EXPECT_CALL(transport_, detach(&exTxn));

  exTxn.setHandler(&handler_);
  exTxn.onError(err);
  // Since the transaction is already closed for ingress, giving it
  // ingress body causes the transaction to be aborted and closed
  // immediately.
  exTxn.onIngressBody(makeBuf(10), 0);

  eventBase_.loop();
}

TEST_F(DownstreamTransactionTest, ParseIngressErrorExTxnNonUnidirectional) {
  // Test where the ex transaction using QoS0 gets Ingress error
  HTTPTransaction exTxn(TransportDirection::DOWNSTREAM,
                        HTTPCodec::StreamID(2),
                        1,
                        transport_,
                        txnEgressQueue_,
                        transactionTimeouts_.get(),
                        std::chrono::milliseconds(500),
                        nullptr,
                        false,
                        0,
                        0,
                        http2::DefaultPriority,
                        HTTPCodec::NoStream,
                        HTTPCodec::ExAttributes(1, true));

  HTTPException err(HTTPException::Direction::INGRESS, "test");
  err.setHttpStatusCode(400);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(&exTxn));
  // Ingress error will propagate
  // even if INGRESS state is completed for unidrectional ex_txn
  EXPECT_CALL(handler_, onError(_))
      .WillOnce(Invoke([](const HTTPException& ex) {
        ASSERT_EQ(ex.getDirection(), HTTPException::Direction::INGRESS);
        ASSERT_EQ(std::string(ex.what()), "test");
      }));

  EXPECT_CALL(transport_, sendAbort(_, _));
  EXPECT_CALL(handler_, detachTransaction());
  EXPECT_CALL(transport_, detach(&exTxn));

  exTxn.setHandler(&handler_);
  exTxn.onError(err);
  // Since the transaction is already closed for ingress, giving it
  // ingress body causes the transaction to be aborted and closed
  // immediately.
  exTxn.onIngressBody(makeBuf(10), 0);

  eventBase_.loop();
}
