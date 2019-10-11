/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/httpserver/RequestHandlerAdaptor.h>
#include "proxygen/httpserver/Mocks.h"
#include "proxygen/lib/http/session/test/HTTPTransactionMocks.h"
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace proxygen;
using namespace testing;

struct StubRequestHandlerAdaptor : public RequestHandlerAdaptor {
  using RequestHandlerAdaptor::RequestHandlerAdaptor;

  void sendHeaders(HTTPMessage& /*msg*/) noexcept override {
    headersSent_ = true;
  }

  void sendEOM() noexcept override {
    // prevent using tx_
  }

  bool headersSent_{false};
};

void testExpectHandling(bool handlerResponds) {
  StrictMock<MockRequestHandler> requestHandler_;
  EXPECT_CALL(requestHandler_, canHandleExpect())
      .WillOnce(Return(handlerResponds));
  EXPECT_CALL(requestHandler_, onRequest(_));
  auto adaptor = std::make_shared<StubRequestHandlerAdaptor>(&requestHandler_);
  auto msg = std::make_unique<HTTPMessage>();
  msg->getHeaders().add("Expect", "100-continue");
  auto txHandler = std::dynamic_pointer_cast<HTTPTransactionHandler>(adaptor);
  txHandler->onHeadersComplete(std::move(msg));
  EXPECT_EQ(adaptor->headersSent_, !handlerResponds);
}

TEST(RequestHandlerAdaptorTest, Expect) {
  testExpectHandling(true /* handlerResponds */);
  testExpectHandling(false /* handlerResponds */);
}

TEST(RequestHandlerAdaptorTest, onTimeoutError) {
  NiceMock<MockRequestHandler> requestHandler_;
  auto adaptor = new RequestHandlerAdaptor(&requestHandler_);
  NiceMock<MockHTTPTransactionTransport> transport;
  HTTP2PriorityQueue egressQueue;
  HTTPTransaction txn(
      TransportDirection::DOWNSTREAM, 1, 1, transport, egressQueue);
  txn.setHandler(adaptor);
  // egress timeout error
  HTTPException ex(HTTPException::Direction::EGRESS, "egress timeout");
  ex.setProxygenError(kErrorTimeout);
  EXPECT_CALL(requestHandler_, onError(kErrorTimeout));
  txn.onError(ex);
}

TEST(RequestHandlerAdaptorTest, onStreamAbortError) {
  NiceMock<MockRequestHandler> requestHandler_;
  auto adaptor = new RequestHandlerAdaptor(&requestHandler_);
  NiceMock<MockHTTPTransactionTransport> transport;
  HTTP2PriorityQueue egressQueue;
  HTTPTransaction txn(
      TransportDirection::DOWNSTREAM, 1, 1, transport, egressQueue);
  txn.setHandler(adaptor);
  // stream abort cancel error
  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                   "stream abort");
  ex.setProxygenError(kErrorStreamAbort);
  // expect notifying the request handler
  EXPECT_CALL(requestHandler_, onError(kErrorStreamAbort));
  txn.onError(ex);
}
