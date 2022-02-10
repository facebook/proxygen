/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
#include <proxygen/lib/http/session/test/MockHTTPSessionStats.h>

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
    setupInvariantViolation();
  }
  MockHTTPHandler(HTTPTransaction& txn,
                  HTTPMessage* msg,
                  const folly::SocketAddress&)
      : HTTPHandlerBase(&txn, msg) {
    setupInvariantViolation();
  }

  void setupInvariantViolation() {
    ON_CALL(*this, _onInvariantViolation(testing::_))
        .WillByDefault(testing::Invoke(
            [](const HTTPException& ex) { LOG(FATAL) << ex.what(); }));
  }

  void setTransaction(HTTPTransaction* txn) noexcept override {
    _setTransaction(txn);
  }
  MOCK_METHOD1(_setTransaction, void(HTTPTransaction*));

  void detachTransaction() noexcept override {
    _detachTransaction();
  }
  MOCK_METHOD0(_detachTransaction, void());

  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    _onHeadersComplete(std::shared_ptr<HTTPMessage>(msg.release()));
  }

  MOCK_METHOD1(_onHeadersComplete, void(std::shared_ptr<HTTPMessage>));

  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    _onBody(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  MOCK_METHOD1(_onBody, void(std::shared_ptr<folly::IOBuf>));

  void onBodyWithOffset(uint64_t bodyOffset,
                        std::unique_ptr<folly::IOBuf> chain) noexcept override {
    _onBodyWithOffset(bodyOffset,
                      std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  MOCK_METHOD2(_onBodyWithOffset,
               void(uint64_t, std::shared_ptr<folly::IOBuf>));

  void onDatagram(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    _onDatagram(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  MOCK_METHOD1(_onDatagram, void(std::shared_ptr<folly::IOBuf>));

  void onChunkHeader(size_t length) noexcept override {
    _onChunkHeader(length);
  }
  MOCK_METHOD1(_onChunkHeader, void(size_t));

  void onChunkComplete() noexcept override {
    _onChunkComplete();
  }
  MOCK_METHOD0(_onChunkComplete, void());

  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    _onTrailers(std::shared_ptr<HTTPHeaders>(trailers.release()));
  }

  MOCK_METHOD1(_onTrailers, void(std::shared_ptr<HTTPHeaders>));

  void onEOM() noexcept override {
    _onEOM();
  }
  MOCK_METHOD0(_onEOM, void());

  void onUpgrade(UpgradeProtocol protocol) noexcept override {
    _onUpgrade(protocol);
  }
  MOCK_METHOD1(_onUpgrade, void(UpgradeProtocol));

  void onError(const HTTPException& error) noexcept override {
    _onError(error);
  }
  MOCK_METHOD1(_onError, void(const HTTPException&));

  void onInvariantViolation(const HTTPException& error) noexcept override {
    _onInvariantViolation(error);
  }
  MOCK_METHOD1(_onInvariantViolation, void(const HTTPException&));

  void onGoaway(ErrorCode errCode) noexcept override {
    _onGoaway(errCode);
  }
  MOCK_METHOD1(_onGoaway, void(ErrorCode));

  void onEgressPaused() noexcept override {
    _onEgressPaused();
  }
  MOCK_METHOD0(_onEgressPaused, void());

  void onEgressResumed() noexcept override {
    _onEgressResumed();
  }
  MOCK_METHOD0(_onEgressResumed, void());

  void onPushedTransaction(HTTPTransaction* txn) noexcept override {
    _onPushedTransaction(txn);
  }
  MOCK_METHOD1(_onPushedTransaction, void(HTTPTransaction*));

  void onExTransaction(HTTPTransaction* txn) noexcept override {
    _onExTransaction(txn);
  }
  MOCK_METHOD1(_onExTransaction, void(HTTPTransaction*));

  void expectTransaction(std::function<void(HTTPTransaction* txn)> callback) {
    EXPECT_CALL(*this, _setTransaction(testing::_))
        .WillOnce(testing::Invoke(callback))
        .RetiresOnSaturation();
  }

  void expectTransaction(HTTPTransaction** pTxn = nullptr) {
    EXPECT_CALL(*this, _setTransaction(testing::_))
        .WillOnce(testing::SaveArg<0>(pTxn ? pTxn : &txn_));
  }

  void expectPushedTransaction(HTTPTransactionHandler* handler = nullptr) {
    EXPECT_CALL(*this, _onPushedTransaction(testing::_))
        .WillOnce(testing::Invoke([handler](HTTPTransaction* txn) {
          if (handler) {
            txn->setHandler(handler);
          }
        }));
  }

  void expectHeaders(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onHeadersComplete(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, _onHeadersComplete(testing::_));
    }
  }

  void expectHeaders(std::function<void(std::shared_ptr<HTTPMessage>)> cb) {
    EXPECT_CALL(*this, _onHeadersComplete(testing::_))
        .WillOnce(testing::Invoke(cb))
        .RetiresOnSaturation();
  }

  void expectTrailers(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onTrailers(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, _onTrailers(testing::_));
    }
  }

  void expectTrailers(
      std::function<void(std::shared_ptr<HTTPHeaders> trailers)> cb) {
    EXPECT_CALL(*this, _onTrailers(testing::_))
        .WillOnce(testing::Invoke(cb))
        .RetiresOnSaturation();
  }

  void expectChunkHeader(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onChunkHeader(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, _onChunkHeader(testing::_));
    }
  }

  void expectBody(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onBodyWithOffset(testing::_, testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, _onBodyWithOffset(testing::_, testing::_));
    }
  }

  void expectBody(
      std::function<void(uint64_t, std::shared_ptr<folly::IOBuf>)> callback) {
    EXPECT_CALL(*this, _onBodyWithOffset(testing::_, testing::_))
        .WillOnce(testing::Invoke(callback));
  }

  void expectDatagram(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onDatagram(testing::_))
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, _onDatagram(testing::_));
    }
  }

  void expectDatagram(
      std::function<void(std::shared_ptr<folly::IOBuf>)> callback) {
    EXPECT_CALL(*this, _onDatagram(testing::_))
        .WillOnce(testing::Invoke(callback));
  }

  void expectChunkComplete(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onChunkComplete())
          .WillOnce(testing::InvokeWithoutArgs(callback));
    } else {
      EXPECT_CALL(*this, _onChunkComplete());
    }
  }

  void expectEOM(std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onEOM()).WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, _onEOM());
    }
  }

  void expectEgressPaused(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onEgressPaused()).WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, _onEgressPaused());
    }
  }

  void expectEgressResumed(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _onEgressResumed())
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, _onEgressResumed());
    }
  }

  void expectError(std::function<void(const HTTPException& ex)> callback =
                       std::function<void(const HTTPException& ex)>()) {
    if (callback) {
      EXPECT_CALL(*this, _onError(testing::_))
          .WillOnce(testing::Invoke(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, _onError(testing::_)).RetiresOnSaturation();
    }
  }

  void expectGoaway(std::function<void(ErrorCode)> callback =
                        std::function<void(ErrorCode)>()) {
    if (callback) {
      EXPECT_CALL(*this, _onGoaway(testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, _onGoaway(testing::_));
    }
  }

  void expectDetachTransaction(
      std::function<void()> callback = std::function<void()>()) {
    if (callback) {
      EXPECT_CALL(*this, _detachTransaction())
          .WillOnce(testing::Invoke(callback))
          .RetiresOnSaturation();
    } else {
      EXPECT_CALL(*this, _detachTransaction()).RetiresOnSaturation();
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

  void setTransaction(HTTPTransaction* txn) noexcept override {
    _setTransaction(txn);
  }
  MOCK_METHOD1(_setTransaction, void(HTTPTransaction*));

  void detachTransaction() noexcept override {
    _detachTransaction();
  }
  MOCK_METHOD0(_detachTransaction, void());

  void onError(const HTTPException& error) noexcept override {
    _onError(error);
  }
  MOCK_METHOD1(_onError, void(const HTTPException&));

  void onGoaway(ErrorCode errCode) noexcept override {
    _onGoaway(errCode);
  }
  MOCK_METHOD1(_onGoaway, void(ErrorCode));

  void onEgressPaused() noexcept override {
    _onEgressPaused();
  }
  MOCK_METHOD0(_onEgressPaused, void());

  void onEgressResumed() noexcept override {
    _onEgressResumed();
  }
  MOCK_METHOD0(_onEgressResumed, void());

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

class MockDSRRequestSender : public DSRRequestSender {
 public:
  MockDSRRequestSender() = default;
};

} // namespace proxygen
