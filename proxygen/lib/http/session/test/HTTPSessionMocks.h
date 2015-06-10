/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gmock/gmock.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/session/HTTPDownstreamSession.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

#define GMOCK_NOEXCEPT_METHOD0(m, F) GMOCK_METHOD0_(, noexcept,, m, F)
#define GMOCK_NOEXCEPT_METHOD1(m, F) GMOCK_METHOD1_(, noexcept,, m, F)

namespace proxygen {

class HTTPHandlerBase {
 public:
  HTTPHandlerBase() {}
  HTTPHandlerBase(HTTPTransaction* txn, HTTPMessage* msg) :
      txn_(txn),
      msg_(msg) {}

  void terminate() {
    txn_->sendAbort();
  }

  void sendHeaders(uint32_t code, uint32_t content_length,
                   bool keepalive=true) {
    HTTPMessage reply;
    reply.setStatusCode(code);
    reply.setHTTPVersion(1, 1);
    reply.setWantsKeepalive(keepalive);
    reply.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH,
                           folly::to<std::string>(content_length));
    txn_->sendHeaders(reply);
  }

  void sendReply() {
    sendReplyCode(200);
  }

  void sendReplyCode(uint32_t code) {
    sendHeaders(code, 0, true);
    txn_->sendEOM();
  }

  void sendBody(uint32_t content_length) {
    while (content_length > 0) {
      uint32_t toSend = std::min(content_length, uint32_t(4096));
      char buf[toSend];
      memset(buf, 'a', toSend);
      txn_->sendBody(std::move(folly::IOBuf::copyBuffer(buf, toSend)));
      content_length -= toSend;
    }
  }

  void sendReplyWithBody(uint32_t code, uint32_t content_length,
                         bool keepalive=true) {
    sendHeaders(code, content_length, keepalive);
    sendBody(content_length);
    txn_->sendEOM();
  }

  void sendChunkedReplyWithBody(uint32_t code, uint32_t content_length,
                                uint32_t chunkSize, bool hasTrailers) {
    HTTPMessage reply;
    reply.setStatusCode(code);
    reply.setHTTPVersion(1, 1);
    reply.setIsChunked(true);
    txn_->sendHeaders(reply);
    while (content_length > 0) {
      uint32_t toSend = std::min(content_length, chunkSize);
      char buf[toSend];
      memset(buf, 'a', toSend);
      txn_->sendChunkHeader(toSend);
      txn_->sendBody(std::move(folly::IOBuf::copyBuffer(buf, toSend)));
      txn_->sendChunkTerminator();
      content_length -= toSend;
    }
    if (hasTrailers) {
      HTTPHeaders trailers;
      trailers.add("X-Trailer1", "Foo");
      txn_->sendTrailers(trailers);
    }
    txn_->sendEOM();
  }

  HTTPTransaction* txn_{nullptr};
  std::shared_ptr<HTTPMessage> msg_;
};

class MockHTTPHandler : public HTTPHandlerBase,
                        public HTTPTransaction::Handler {
 public:
  MockHTTPHandler() {}
  MockHTTPHandler(HTTPTransaction& txn, HTTPMessage* msg,
                  const folly::SocketAddress&) :
      HTTPHandlerBase(&txn, msg) {}

  GMOCK_NOEXCEPT_METHOD1(setTransaction, void(HTTPTransaction* txn));

  GMOCK_NOEXCEPT_METHOD0(detachTransaction, void());

  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    onHeadersComplete(std::shared_ptr<HTTPMessage>(msg.release()));
  }

  GMOCK_NOEXCEPT_METHOD1(onHeadersComplete,
                         void(std::shared_ptr<HTTPMessage> msg));

  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    onBody(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  GMOCK_NOEXCEPT_METHOD1(onBody, void(std::shared_ptr<folly::IOBuf> chain));

  GMOCK_NOEXCEPT_METHOD1(onChunkHeader, void(size_t length));

  GMOCK_NOEXCEPT_METHOD0(onChunkComplete, void());

  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    onTrailers(std::shared_ptr<HTTPHeaders>(trailers.release()));
  }

  GMOCK_NOEXCEPT_METHOD1(onTrailers,
                         void(std::shared_ptr<HTTPHeaders> trailers));

  GMOCK_NOEXCEPT_METHOD0(onEOM, void());

  GMOCK_NOEXCEPT_METHOD1(onUpgrade, void(UpgradeProtocol protocol));

  GMOCK_NOEXCEPT_METHOD1(onError, void(const HTTPException& error));

  GMOCK_NOEXCEPT_METHOD0(onEgressPaused, void());

  GMOCK_NOEXCEPT_METHOD0(onEgressResumed, void());

  GMOCK_NOEXCEPT_METHOD1(onPushedTransaction,
                         void(HTTPTransaction*));
};

class MockHTTPPushHandler : public HTTPHandlerBase,
                            public HTTPTransaction::PushHandler {
 public:
  MockHTTPPushHandler() {}
  MockHTTPPushHandler(HTTPTransaction& txn, HTTPMessage* msg,
                  const folly::SocketAddress&) :
      HTTPHandlerBase(&txn, msg) {}

  GMOCK_NOEXCEPT_METHOD1(setTransaction, void(HTTPTransaction* txn));

  GMOCK_NOEXCEPT_METHOD0(detachTransaction, void());

  GMOCK_NOEXCEPT_METHOD1(onError, void(const HTTPException& error));

  GMOCK_NOEXCEPT_METHOD0(onEgressPaused, void());

  GMOCK_NOEXCEPT_METHOD0(onEgressResumed, void());

  void sendPushHeaders(const std::string& path,
                       const std::string& host,
                       uint32_t content_length) {
    HTTPMessage push;
    push.setURL(path);
    push.getHeaders().set(HTTP_HEADER_HOST, host);
    push.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH,
                          folly::to<std::string>(content_length));
    txn_->sendHeaders(push);
  }
};

class MockController : public HTTPSessionController {
 public:
  MOCK_METHOD2(getRequestHandler, HTTPTransactionHandler*(
                 HTTPTransaction&, HTTPMessage* msg));

  MOCK_METHOD3(getParseErrorHandler, HTTPTransactionHandler*(
      HTTPTransaction*,
      const HTTPException&,
      const folly::SocketAddress&));

  MOCK_METHOD2(getTransactionTimeoutHandler, HTTPTransactionHandler*(
      HTTPTransaction* txn,
      const folly::SocketAddress&));

  MOCK_METHOD1(attachSession, void(HTTPSession*));
  MOCK_METHOD1(detachSession, void(const HTTPSession*));
};

ACTION_P(ExpectString, expected) {
  std::string bodystr((const char *)arg0->data(), arg0->length());
  EXPECT_EQ(bodystr, expected);
}

class MockHTTPSessionInfoCallback: public HTTPSession::InfoCallback {
 public:
  MOCK_METHOD1(onCreate, void(const HTTPSession&));
  MOCK_METHOD2(onIngressError, void(const HTTPSession&,
                                    ProxygenError));
  MOCK_METHOD2(onRead, void(const HTTPSession&, size_t));
  MOCK_METHOD2(onWrite, void(const HTTPSession&, size_t));
  MOCK_METHOD1(onRequestBegin, void(const HTTPSession&));
  MOCK_METHOD2(onRequestEnd, void(const HTTPSession&, uint32_t));
  MOCK_METHOD1(onActivateConnection, void(const HTTPSession&));
  MOCK_METHOD1(onDeactivateConnection, void(const HTTPSession&));
  MOCK_METHOD1(onDestroy, void(const HTTPSession&));
  MOCK_METHOD2(onIngressMessage, void(const HTTPSession&,
                                      const HTTPMessage&));
  MOCK_METHOD1(onIngressLimitExceeded, void(const HTTPSession&));
  MOCK_METHOD1(onIngressPaused, void(const HTTPSession&));
  MOCK_METHOD1(onTransactionDetached, void(const HTTPSession&));
  MOCK_METHOD1(onPingReplySent, void(int64_t));
  MOCK_METHOD0(onPingReplyReceived, void());
  MOCK_METHOD1(onSettingsOutgoingStreamsFull, void(const HTTPSession&));
  MOCK_METHOD1(onSettingsOutgoingStreamsNotFull, void(const HTTPSession&));
};

}
