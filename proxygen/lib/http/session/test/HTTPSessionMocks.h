/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/session/HTTPDownstreamSession.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

#define GMOCK_NOEXCEPT_METHOD0(m, F) GMOCK_METHOD0_(, noexcept, , m, F)
#define GMOCK_NOEXCEPT_METHOD1(m, F) GMOCK_METHOD1_(, noexcept, , m, F)
#define GMOCK_NOEXCEPT_METHOD2(m, F) GMOCK_METHOD2_(, noexcept, , m, F)

namespace proxygen {

class HTTPHandlerBase {
 public:
  HTTPHandlerBase() {
  }
  HTTPHandlerBase(HTTPTransaction* txn, HTTPMessage* msg)
      : txn_(txn), msg_(msg) {
  }

  void terminate() {
    txn_->sendAbort();
  }

  void sendRequest() {
    sendRequest(getGetRequest());
  }

  void sendRequest(HTTPMessage req) {
    // this copies but it's test code meh
    txn_->sendHeaders(req);
    txn_->sendEOM();
  }

  using HeaderMap = std::map<std::string, std::string>;
  void sendHeaders(uint32_t code,
                   uint32_t content_length,
                   bool keepalive = true,
                   HeaderMap headers = HeaderMap()) {
    HTTPMessage reply;
    reply.setStatusCode(code);
    reply.setHTTPVersion(1, 1);
    reply.setWantsKeepalive(keepalive);
    reply.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH,
                           folly::to<std::string>(content_length));
    for (auto& nv : headers) {
      reply.getHeaders().add(nv.first, nv.second);
    }
    txn_->sendHeaders(reply);
  }

  bool sendHeadersWithDelegate(uint32_t code,
                               uint32_t content_length,
                               std::unique_ptr<DSRRequestSender> dsrSender) {
    HTTPMessage response;
    response.setStatusCode(code);
    response.setHTTPVersion(1, 1);
    response.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH,
                              folly::to<std::string>(content_length));
    return txn_->sendHeadersWithDelegate(response, std::move(dsrSender));
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
      txn_->sendBody(folly::IOBuf::copyBuffer(buf, toSend));
      content_length -= toSend;
    }
  }

  void sendBodyWithLastByteFlushedTracking(uint32_t content_length) {
    txn_->setLastByteFlushedTrackingEnabled(true);
    sendBody(content_length);
  }

  void sendReplyWithBody(uint32_t code,
                         uint32_t content_length,
                         bool keepalive = true,
                         bool sendEOM = true,
                         bool hasTrailers = false) {
    sendHeaders(code, content_length, keepalive);
    sendBody(content_length);
    if (hasTrailers) {
      HTTPHeaders trailers;
      trailers.add("X-Trailer1", "Foo");
      txn_->sendTrailers(trailers);
    }
    if (sendEOM) {
      txn_->sendEOM();
    }
  }

  void sendEOM() {
    txn_->sendEOM();
  }

  void sendChunkedReplyWithBody(uint32_t code,
                                uint32_t content_length,
                                uint32_t chunkSize,
                                bool hasTrailers,
                                bool sendEOM = true) {
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
      txn_->sendBody(folly::IOBuf::copyBuffer(buf, toSend));
      txn_->sendChunkTerminator();
      content_length -= toSend;
    }
    if (hasTrailers) {
      HTTPHeaders trailers;
      trailers.add("X-Trailer1", "Foo");
      txn_->sendTrailers(trailers);
    }
    if (sendEOM) {
      txn_->sendEOM();
    }
  }

  HTTPTransaction* txn_{nullptr};
  HTTPTransaction* pushedTxn_{nullptr};

  std::shared_ptr<HTTPMessage> msg_;
};

class MockHTTPHandler
    : public HTTPHandlerBase
    , public HTTPTransaction::Handler {
 public:
  MockHTTPHandler() {
  }
  MockHTTPHandler(HTTPTransaction& txn,
                  HTTPMessage* msg,
                  const folly::SocketAddress&)
      : HTTPHandlerBase(&txn, msg) {
  }

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

  void onBodyWithOffset(uint64_t bodyOffset,
                        std::unique_ptr<folly::IOBuf> chain) noexcept override {
    onBodyWithOffset(bodyOffset,
                     std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  GMOCK_NOEXCEPT_METHOD2(onBodyWithOffset,
                         void(uint64_t bodyOffset,
                              std::shared_ptr<folly::IOBuf> chain));

  void onDatagram(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    onDatagram(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  GMOCK_NOEXCEPT_METHOD1(onDatagram, void(std::shared_ptr<folly::IOBuf> chain));

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

  GMOCK_NOEXCEPT_METHOD1(onGoaway, void(ErrorCode));

  GMOCK_NOEXCEPT_METHOD0(onEgressPaused, void());

  GMOCK_NOEXCEPT_METHOD0(onEgressResumed, void());

  GMOCK_NOEXCEPT_METHOD1(onPushedTransaction, void(HTTPTransaction*));

  GMOCK_NOEXCEPT_METHOD1(onExTransaction, void(HTTPTransaction*));

  GMOCK_NOEXCEPT_METHOD1(traceEventAvailable, void(TraceEvent));

  void expectTransaction(std::function<void(HTTPTransaction* txn)> callback) {
    EXPECT_CALL(*this, setTransaction(testing::_))
        .WillOnce(testing::Invoke(callback))
        .RetiresOnSaturation();
  }

  void expectTransaction(HTTPTransaction** pTxn = nullptr) {
    EXPECT_CALL(*this, setTransaction(testing::_))
        .WillOnce(testing::SaveArg<0>(pTxn ? pTxn : &txn_));
  }

  void expectPushedTransaction(HTTPTransactionHandler* handler = nullptr) {
    EXPECT_CALL(*this, onPushedTransaction(testing::_))
        .WillOnce(testing::Invoke([handler](HTTPTransaction* txn) {
          if (handler) {
            txn->setHandler(handler);
          }
        }));
  }

  void expectHeaders(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onHeadersComplete(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, onHeadersComplete(testing::_));
    }
  }

  void expectHeaders(std::function<void(std::shared_ptr<HTTPMessage>)> cb) {
    EXPECT_CALL(*this, onHeadersComplete(testing::_))
        .WillOnce(testing::Invoke(cb))
        .RetiresOnSaturation();
  }

  void expectTrailers(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onTrailers(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, onTrailers(testing::_));
    }
  }

  void expectTrailers(
      std::function<void(std::shared_ptr<HTTPHeaders> trailers)> cb) {
    EXPECT_CALL(*this, onTrailers(testing::_))
        .WillOnce(testing::Invoke(cb))
        .RetiresOnSaturation();
  }

  void expectChunkHeader(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onChunkHeader(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, onChunkHeader(testing::_));
    }
  }

  void expectBody(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onBodyWithOffset(testing::_, testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, onBodyWithOffset(testing::_, testing::_));
    }
  }

  void expectBody(
      std::function<void(uint64_t, std::shared_ptr<folly::IOBuf>)> callback) {
    EXPECT_CALL(*this, onBodyWithOffset(testing::_, testing::_))
        .WillOnce(testing::Invoke(callback));
  }

  void expectDatagram(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onDatagram(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, onDatagram(testing::_));
    }
  }

  void expectDatagram(
      std::function<void(std::shared_ptr<folly::IOBuf>)> callback) {
    EXPECT_CALL(*this, onDatagram(testing::_))
        .WillOnce(testing::Invoke(callback));
  }

  void expectChunkComplete(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onChunkComplete())
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, onChunkComplete());
    }
  }

  void expectEOM(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onEOM()).WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onEOM());
    }
  }

  void expectEgressPaused(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onEgressPaused()).WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onEgressPaused());
    }
  }

  void expectEgressResumed(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, onEgressResumed()).WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onEgressResumed());
    }
  }

  void expectError(std::function<void(const HTTPException& ex)> callback =
                       std::function<void(const HTTPException& ex)>()) {
    if (callback) {
      EXPECT_CALL(*this, onError(testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onError(testing::_));
    }
  }

  void expectGoaway(std::function<void(ErrorCode)> callback =
                        std::function<void(ErrorCode)>()) {
    if (callback) {
      EXPECT_CALL(*this, onGoaway(testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onGoaway(testing::_));
    }
  }

  void expectDetachTransaction(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, detachTransaction())
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, detachTransaction());
    }
  }
};

class MockHTTPPushHandler
    : public HTTPHandlerBase
    , public HTTPTransaction::PushHandler {
 public:
  MockHTTPPushHandler() {
  }
  MockHTTPPushHandler(HTTPTransaction& txn,
                      HTTPMessage* msg,
                      const folly::SocketAddress&)
      : HTTPHandlerBase(&txn, msg) {
  }

  GMOCK_NOEXCEPT_METHOD1(setTransaction, void(HTTPTransaction* txn));

  GMOCK_NOEXCEPT_METHOD0(detachTransaction, void());

  GMOCK_NOEXCEPT_METHOD1(onError, void(const HTTPException& error));

  GMOCK_NOEXCEPT_METHOD1(onGoaway, void(ErrorCode));

  GMOCK_NOEXCEPT_METHOD0(onEgressPaused, void());

  GMOCK_NOEXCEPT_METHOD0(onEgressResumed, void());

  void sendPushHeaders(const std::string& path,
                       const std::string& host,
                       uint32_t content_length,
                       http2::PriorityUpdate pri) {
    HTTPMessage push;
    push.setURL(path);
    push.getHeaders().set(HTTP_HEADER_HOST, host);
    push.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH,
                          folly::to<std::string>(content_length));
    push.setHTTP2Priority(
        std::make_tuple(pri.streamDependency, pri.exclusive, pri.weight));
    txn_->sendHeaders(push);
  }
};

class MockController : public HTTPSessionController {
 public:
  MOCK_METHOD2(getRequestHandler,
               HTTPTransactionHandler*(HTTPTransaction&, HTTPMessage* msg));

  MOCK_METHOD3(getParseErrorHandler,
               HTTPTransactionHandler*(HTTPTransaction*,
                                       const HTTPException&,
                                       const folly::SocketAddress&));

  MOCK_METHOD2(getTransactionTimeoutHandler,
               HTTPTransactionHandler*(HTTPTransaction* txn,
                                       const folly::SocketAddress&));

  MOCK_METHOD1(attachSession, void(HTTPSessionBase*));
  MOCK_METHOD1(detachSession, void(const HTTPSessionBase*));
  MOCK_METHOD1(onSessionCodecChange, void(HTTPSessionBase*));
  MOCK_METHOD1(onTransportReady, void(HTTPSessionBase*));

  MOCK_CONST_METHOD0(getGracefulShutdownTimeout, std::chrono::milliseconds());

  MOCK_CONST_METHOD0(getHeaderIndexingStrategy,
                     const HeaderIndexingStrategy*());
};

class MockUpstreamController : public HTTPUpstreamSessionController {
 public:
  MOCK_METHOD1(attachSession, void(HTTPSessionBase*));
  MOCK_METHOD1(detachSession, void(const HTTPSessionBase*));
  MOCK_METHOD1(onSessionCodecChange, void(HTTPSessionBase*));

  MOCK_CONST_METHOD0(getHeaderIndexingStrategy,
                     const HeaderIndexingStrategy*());
};

ACTION_P(ExpectString, expected) {
  std::string bodystr((const char*)arg1->data(), arg1->length());
  EXPECT_EQ(bodystr, expected);
}

ACTION_P(ExpectBodyLen, expectedLen) {
  EXPECT_EQ(arg1->computeChainDataLength(), expectedLen);
}

class MockHTTPSessionInfoCallback : public HTTPSession::InfoCallback {
 public:
  MOCK_METHOD1(onCreate, void(const HTTPSessionBase&));
  MOCK_METHOD1(onTransportReady, void(const HTTPSessionBase&));
  MOCK_METHOD1(onConnectionError, void(const HTTPSessionBase&));
  MOCK_METHOD2(onIngressError, void(const HTTPSessionBase&, ProxygenError));
  MOCK_METHOD0(onIngressEOF, void());
  MOCK_METHOD2(onRead, void(const HTTPSessionBase&, size_t));
  MOCK_METHOD3(onRead,
               void(const HTTPSessionBase&,
                    size_t,
                    folly::Optional<HTTPCodec::StreamID>));
  MOCK_METHOD2(onWrite, void(const HTTPSessionBase&, size_t));
  MOCK_METHOD1(onRequestBegin, void(const HTTPSessionBase&));
  MOCK_METHOD2(onRequestEnd, void(const HTTPSessionBase&, uint32_t));
  MOCK_METHOD1(onActivateConnection, void(const HTTPSessionBase&));
  MOCK_METHOD1(onDeactivateConnection, void(const HTTPSessionBase&));
  MOCK_METHOD1(onDestroy, void(const HTTPSessionBase&));
  MOCK_METHOD2(onIngressMessage,
               void(const HTTPSessionBase&, const HTTPMessage&));
  MOCK_METHOD1(onIngressLimitExceeded, void(const HTTPSessionBase&));
  MOCK_METHOD1(onIngressPaused, void(const HTTPSessionBase&));
  MOCK_METHOD1(onTransactionDetached, void(const HTTPSessionBase&));
  MOCK_METHOD1(onPingReplySent, void(int64_t));
  MOCK_METHOD0(onPingReplyReceived, void());
  MOCK_METHOD1(onSettingsOutgoingStreamsFull, void(const HTTPSessionBase&));
  MOCK_METHOD1(onSettingsOutgoingStreamsNotFull, void(const HTTPSessionBase&));
  MOCK_METHOD1(onFlowControlWindowClosed, void(const HTTPSessionBase&));
  MOCK_METHOD1(onEgressBuffered, void(const HTTPSessionBase&));
  MOCK_METHOD1(onEgressBufferCleared, void(const HTTPSessionBase&));
  MOCK_METHOD2(onSettings, void(const HTTPSessionBase&, const SettingsList&));
  MOCK_METHOD1(onSettingsAck, void(const HTTPSessionBase&));
};

class DummyHTTPSessionStats : public HTTPSessionStats {
 public:
  void recordTransactionOpened() noexcept override{};
  void recordTransactionClosed() noexcept override{};
  void recordTransactionsServed(uint64_t) noexcept override{};
  void recordSessionReused() noexcept override{};
  // virtual void recordSessionIdleTime(std::chrono::seconds) noexcept {};
  void recordTransactionStalled() noexcept override{};
  void recordSessionStalled() noexcept override{};

  void recordPresendIOSplit() noexcept override{};
  void recordPresendExceedLimit() noexcept override{};
  void recordTTLBAExceedLimit() noexcept override{};
  void recordTTLBANotFound() noexcept override{};
  void recordTTLBAReceived() noexcept override{};
  void recordTTLBATimeout() noexcept override{};
  void recordTTLBATracked() noexcept override{};
  void recordTTBTXExceedLimit() noexcept override{};
  void recordTTBTXReceived() noexcept override{};
  void recordTTBTXTimeout() noexcept override{};
  void recordTTBTXNotFound() noexcept override{};
  void recordTTBTXTracked() noexcept override{};
};

class MockHTTPSessionStats : public DummyHTTPSessionStats {
 public:
  MockHTTPSessionStats() {
  }
  GMOCK_NOEXCEPT_METHOD0(recordTransactionOpened, void());
  GMOCK_NOEXCEPT_METHOD0(recordTransactionClosed, void());
  GMOCK_NOEXCEPT_METHOD1(recordTransactionsServed, void(uint64_t));
  GMOCK_NOEXCEPT_METHOD0(recordSessionReused, void());
  GMOCK_NOEXCEPT_METHOD1(recordSessionIdleTime, void(std::chrono::seconds));
  GMOCK_NOEXCEPT_METHOD0(recordTransactionStalled, void());
  GMOCK_NOEXCEPT_METHOD0(recordSessionStalled, void());
};

class MockDSRRequestSender : public DSRRequestSender {
 public:
  MockDSRRequestSender() = default;
};

} // namespace proxygen
