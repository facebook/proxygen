/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxygen/lib/http/codec/SPDYConstants.h"
#include "proxygen/lib/http/codec/test/MockHTTPCodec.h"
#include "proxygen/lib/http/codec/test/TestUtils.h"
#include "proxygen/lib/http/session/test/HTTPSessionMocks.h"
#include "proxygen/lib/http/session/test/HTTPTransactionMocks.h"
#include "proxygen/lib/test/TestAsyncTransport.h"

#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp/test/MockTAsyncTransport.h>

using namespace apache::thrift::async;
using namespace apache::thrift::transport;
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

  void setupRequestResponseFlow(HTTPTransaction* txn, uint32_t size) {
    EXPECT_CALL(transport_, describe(_))
      .WillRepeatedly(Return());
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
    EXPECT_CALL(transport_, sendHeaders(txn, _, _))
      .WillOnce(Invoke([=](Unused, const HTTPMessage& headers, Unused) {
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
            txn->onIngressBody(makeBuf(size));
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
            txn->onWriteReady(size);
          }))
      .WillOnce(DoDefault()); // The second call is for sending the eom

    txn->setHandler(&handler_);
  }

 protected:
  folly::EventBase eventBase_;
  TAsyncTimeoutSet::UniquePtr transactionTimeouts_{
    new TAsyncTimeoutSet(&eventBase_, std::chrono::milliseconds(500))};
  MockHTTPTransactionTransport transport_;
  StrictMock<MockHTTPHandler> handler_;
  HTTPTransaction::PriorityQueue txnEgressQueue_;
  uint32_t received_{0};
  uint32_t sent_{0};
};

/**
  * Test that the the transaction properly forwards callbacks to the
  * handler and that it interacts with its transport as expected.
  */
TEST_F(DownstreamTransactionTest, simple_callback_forwarding) {
  // flow control is disabled
  auto txn = new HTTPTransaction(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, transactionTimeouts_.get());
  setupRequestResponseFlow(txn, 100);

  txn->onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing that we're sending a window update for simple requests
 */
TEST_F(DownstreamTransactionTest, regular_window_update) {
  auto txn = new HTTPTransaction(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, transactionTimeouts_.get(),
    nullptr,
    true, // flow control enabled
    400,
    spdy::kInitialWindow);
  uint32_t reqBodySize = 220;
  setupRequestResponseFlow(txn, reqBodySize);

  // test that the window update is generated
  EXPECT_CALL(transport_, sendWindowUpdate(_, reqBodySize));

  // run the test
  txn->onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing window increase using window update; we're actually using this in
 * production to avoid bumping the window using the SETTINGS frame
 */
TEST_F(DownstreamTransactionTest, window_increase) {
  // set initial window size higher than per-stream window
  auto txn = new HTTPTransaction(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, transactionTimeouts_.get(),
    nullptr,
    true, // flow control enabled
    spdy::kInitialWindow,
    spdy::kInitialWindow);
  uint32_t reqSize = 500;
  setupRequestResponseFlow(txn, reqSize);

  // use a higher window
  uint32_t perStreamWindow = spdy::kInitialWindow + 1024 * 1024;
  txn->setReceiveWindow(perStreamWindow);

  // we expect the difference from the per stream window and the initial window,
  // together with the bytes sent in the request
  uint32_t expectedWindowUpdate =
    perStreamWindow - spdy::kInitialWindow + reqSize;
  EXPECT_CALL(transport_, sendWindowUpdate(_, expectedWindowUpdate));

  txn->onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

/**
 * Testing that we're not sending window update when per-stream window size is
 * smaller than the initial window size
 */
TEST_F(DownstreamTransactionTest, window_decrease) {
  // set initial window size higher than per-stream window
  auto txn = new HTTPTransaction(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, transactionTimeouts_.get(),
    nullptr,
    true, // flow control enabled
    spdy::kInitialWindow,
    spdy::kInitialWindow);
  setupRequestResponseFlow(txn, 500);

  // in this case, there should be no window update, as we decrease the window
  // below the number of bytes we're sending
  EXPECT_CALL(transport_, sendWindowUpdate(_, _)).Times(0);

  // use a smaller window
  uint32_t perStreamWindow = spdy::kInitialWindow - 1000;
  txn->setReceiveWindow(perStreamWindow);

  txn->onIngressHeadersComplete(makeGetRequest());
  eventBase_.loop();
}

TEST_F(DownstreamTransactionTest, parse_error_cbs) {
  // Test where the transaction gets on parse error and then a body
  // callback. This is possible because codecs are stateless between
  // frames.

  auto txn = new HTTPTransaction(
    TransportDirection::DOWNSTREAM,
    HTTPCodec::StreamID(1), 1, transport_,
    txnEgressQueue_, transactionTimeouts_.get());

  HTTPException err(HTTPException::Direction::INGRESS);
  err.setHttpStatusCode(400);

  InSequence dummy;

  EXPECT_CALL(handler_, setTransaction(txn));
  EXPECT_CALL(handler_, onError(_))
    .WillOnce(Invoke([] (const HTTPException& ex) {
          ASSERT_EQ(ex.getDirection(), HTTPException::Direction::INGRESS);
        }));
  // onBody() is suppressed since ingress is complete after ingress onError()
  // onEOM() is suppressed since ingress is complete after ingress onError()
  EXPECT_CALL(transport_, sendAbort(_, _));
  EXPECT_CALL(handler_, detachTransaction());
  EXPECT_CALL(transport_, detach(txn));

  txn->setHandler(&handler_);
  txn->onError(err);
  // Since the transaction is already closed for ingress, giving it
  // ingress body causes the transaction to be aborted and closed
  // immediately.
  txn->onIngressBody(makeBuf(10));

  eventBase_.loop();
}
