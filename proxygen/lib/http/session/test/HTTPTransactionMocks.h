/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/codec/test/MockHTTPCodec.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace proxygen {

#if defined(__clang__) && __clang_major__ >= 3 && __clang_minor__ >= 6
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

class MockHTTPTransactionTransport : public HTTPTransaction::Transport {
 public:
  MockHTTPTransactionTransport() {
    EXPECT_CALL(*this, getCodecNonConst())
        .WillRepeatedly(testing::ReturnRef(mockCodec_));
  }
  MOCK_METHOD(void, pauseIngress, (HTTPTransaction*), (noexcept));
  MOCK_METHOD(void, resumeIngress, (HTTPTransaction*), (noexcept));
  MOCK_METHOD(void, transactionTimeout, (HTTPTransaction*), (noexcept));
  MOCK_METHOD(void, sendHeaders,
              (HTTPTransaction*, const HTTPMessage&, HTTPHeaderSize*, bool),
              (noexcept));
  MOCK_METHOD(size_t, sendBody,
              (HTTPTransaction*, std::shared_ptr<folly::IOBuf>, bool, bool),
              (noexcept));

  size_t sendBody(HTTPTransaction* txn,
                  std::unique_ptr<folly::IOBuf> iob,
                  bool eom,
                  bool trackLastByteFlushed) noexcept override {
    return sendBody(txn,
                    std::shared_ptr<folly::IOBuf>(iob.release()),
                    eom,
                    trackLastByteFlushed);
  }

  MOCK_METHOD(HTTPSessionBase*, getHTTPSessionBase, (), ());
  MOCK_METHOD(size_t, sendChunkHeader, (HTTPTransaction*, size_t), (noexcept));
  MOCK_METHOD(size_t, sendChunkTerminator, (HTTPTransaction*), (noexcept));
  MOCK_METHOD(size_t, sendEOM, (HTTPTransaction*, const HTTPHeaders*),
              (noexcept));
  MOCK_METHOD(size_t, sendAbort, (HTTPTransaction*, ErrorCode), (noexcept));
  MOCK_METHOD(size_t, sendPriority,
              (HTTPTransaction*, const http2::PriorityUpdate&), (noexcept));
  MOCK_METHOD(void, notifyPendingEgress, (), (noexcept));
  MOCK_METHOD(void, detach, (HTTPTransaction*), (noexcept));
  MOCK_METHOD(size_t, sendWindowUpdate, (HTTPTransaction*, uint32_t),
              (noexcept));
  MOCK_METHOD(void, notifyIngressBodyProcessed, (uint32_t), (noexcept));
  MOCK_METHOD(void, notifyEgressBodyBuffered, (int64_t), (noexcept));
  MOCK_METHOD(const folly::SocketAddress&, getLocalAddressNonConst, (),
              (noexcept));
  MOCK_METHOD(HTTPTransaction*, newPushedTransaction,
              (HTTPCodec::StreamID assocStreamId,
               HTTPTransaction::PushHandler* handler,
               ProxygenError* error), (noexcept));
  MOCK_METHOD(HTTPTransaction*, newExTransaction,
              (HTTPTransaction::Handler* handler,
               HTTPCodec::StreamID controlStream,
               bool unidirectional), (noexcept));

  const folly::SocketAddress& getLocalAddress() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getLocalAddressNonConst();
  }
  MOCK_METHOD(const folly::SocketAddress&, getPeerAddressNonConst, (),
              (noexcept));
  const folly::SocketAddress& getPeerAddress() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getPeerAddressNonConst();
  }
  MOCK_CONST_METHOD1(describe, void(std::ostream&));
  MOCK_METHOD(const wangle::TransportInfo&, getSetupTransportInfoNonConst, (),
              (noexcept));
  const wangle::TransportInfo& getSetupTransportInfo() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getSetupTransportInfoNonConst();
  }

  MOCK_METHOD1(getCurrentTransportInfo, bool(wangle::TransportInfo*));
  MOCK_METHOD1(getFlowControlInfo, void(HTTPTransaction::FlowControlInfo*));

  MOCK_METHOD(HTTPTransaction::Transport::Type, getSessionTypeNonConst, (),
              (noexcept));
  HTTPTransaction::Transport::Type getSessionType() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getSessionTypeNonConst();
  }
  MOCK_METHOD(const HTTPCodec&, getCodecNonConst, (), (noexcept));
  const HTTPCodec& getCodec() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)->getCodecNonConst();
  }
  MOCK_METHOD0(drain, void());
  MOCK_CONST_METHOD0(isDraining, bool());
  MOCK_CONST_METHOD0(getSecurityProtocol, std::string());

  MOCK_CONST_METHOD0(getTransport, const folly::AsyncTransport*());
  MOCK_METHOD0(getTransport, folly::AsyncTransport*());

  MOCK_METHOD(void, addWaitingForReplaySafety,
              (folly::AsyncTransport::ReplaySafetyCallback*), (noexcept));
  MOCK_METHOD(void, removeWaitingForReplaySafety,
              (folly::AsyncTransport::ReplaySafetyCallback*), (noexcept));
  MOCK_CONST_METHOD0(needToBlockForReplaySafety, bool());

  MOCK_METHOD(const folly::AsyncTransport*, getUnderlyingTransportNonConst, (),
              (noexcept));
  const folly::AsyncTransport* getUnderlyingTransport() const
      noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getUnderlyingTransportNonConst();
  }
  MOCK_CONST_METHOD0(isReplaySafe, bool());
  MOCK_METHOD1(setHTTP2PrioritiesEnabled, void(bool));
  MOCK_CONST_METHOD0(getHTTP2PrioritiesEnabled, bool());

  MOCK_METHOD1(getHTTPPriority,
               folly::Optional<const HTTPMessage::HTTPPriority>(uint8_t level));

  MOCK_METHOD1(
      peek,
      folly::Expected<folly::Unit, ErrorCode>(
          const folly::Function<void(
              HTTPCodec::StreamID, uint64_t, const folly::IOBuf&) const>&));

  MOCK_METHOD1(consume, folly::Expected<folly::Unit, ErrorCode>(size_t));

  MOCK_METHOD2(skipBodyTo,
               folly::Expected<folly::Optional<uint64_t>, ErrorCode>(
                   HTTPTransaction*, uint64_t));

  MOCK_METHOD2(rejectBodyTo,
               folly::Expected<folly::Optional<uint64_t>, ErrorCode>(
                   HTTPTransaction*, uint64_t));

  MOCK_METHOD(folly::Optional<HTTPTransaction::ConnectionToken>,
              getConnectionTokenNonConst, (), (noexcept));
  folly::Optional<HTTPTransaction::ConnectionToken> getConnectionToken() const
      noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
        ->getConnectionTokenNonConst();
  }

  void setConnectionToken(HTTPTransaction::ConnectionToken token) {
    EXPECT_CALL(*this, getConnectionTokenNonConst())
        .WillRepeatedly(testing::Return(token));
  }

  MockHTTPCodec mockCodec_;
};

class MockHTTPTransaction : public HTTPTransaction {
 public:
  MockHTTPTransaction(
      TransportDirection direction,
      HTTPCodec::StreamID id,
      uint32_t seqNo,
      // Must be const for gmock
      const HTTP2PriorityQueue& egressQueue,
      folly::HHWheelTimer* timer = nullptr,
      const folly::Optional<std::chrono::milliseconds>& transactionTimeout =
          folly::Optional<std::chrono::milliseconds>(),
      HTTPSessionStats* stats = nullptr,
      bool useFlowControl = false,
      uint32_t receiveInitialWindowSize = 0,
      uint32_t sendInitialWindowSize = 0,
      http2::PriorityUpdate priority = http2::DefaultPriority,
      folly::Optional<HTTPCodec::StreamID> assocStreamId = HTTPCodec::NoStream,
      folly::Optional<HTTPCodec::ExAttributes> exAttributes =
          HTTPCodec::NoExAttributes)
      : HTTPTransaction(direction,
                        id,
                        seqNo,
                        mockTransport_,
                        const_cast<HTTP2PriorityQueue&>(egressQueue),
                        timer,
                        transactionTimeout,
                        stats,
                        useFlowControl,
                        receiveInitialWindowSize,
                        sendInitialWindowSize,
                        priority,
                        assocStreamId,
                        exAttributes),
        defaultAddress_("127.0.0.1", 80) {
    EXPECT_CALL(mockTransport_, getLocalAddressNonConst())
        .WillRepeatedly(testing::ReturnRef(defaultAddress_));
    EXPECT_CALL(mockTransport_, getPeerAddressNonConst())
        .WillRepeatedly(testing::ReturnRef(defaultAddress_));
    EXPECT_CALL(mockTransport_, getCodecNonConst())
        .WillRepeatedly(testing::ReturnRef(mockCodec_));
    EXPECT_CALL(mockTransport_, getSetupTransportInfoNonConst())
        .WillRepeatedly(testing::ReturnRef(setupTransportInfo_));

    // Some tests unfortunately require a half-mocked HTTPTransaction.
    ON_CALL(*this, setHandler(testing::_))
        .WillByDefault(testing::Invoke([this](HTTPTransactionHandler* handler) {
          this->setHandlerUnmocked(handler);
        }));

    // By default we expect canSendHeaders in test to return true
    // Tests that specifically require canSendHeaders to return false need
    // to set the behavior locally.  This is due to the fact that the mocked
    // methods below imply internal state is not correctly tracked/managed
    // in the context of tests
    ON_CALL(*this, canSendHeaders()).WillByDefault(testing::Return(true));
  }

  MockHTTPTransaction(
      TransportDirection direction,
      HTTPCodec::StreamID id,
      uint32_t seqNo,
      // Must be const for gmock
      const HTTP2PriorityQueue& egressQueue,
      const WheelTimerInstance& timeout,
      HTTPSessionStats* stats = nullptr,
      bool useFlowControl = false,
      uint32_t receiveInitialWindowSize = 0,
      uint32_t sendInitialWindowSize = 0,
      http2::PriorityUpdate priority = http2::DefaultPriority,
      folly::Optional<HTTPCodec::StreamID> assocStreamId = HTTPCodec::NoStream,
      folly::Optional<HTTPCodec::ExAttributes> exAttributes =
          HTTPCodec::NoExAttributes)
      : MockHTTPTransaction(direction,
                            id,
                            seqNo,
                            egressQueue,
                            timeout.getWheelTimer(),
                            timeout.getDefaultTimeout(),
                            stats,
                            useFlowControl,
                            receiveInitialWindowSize,
                            sendInitialWindowSize,
                            priority,
                            assocStreamId,
                            exAttributes) {
  }

  MockHTTPTransaction(TransportDirection direction,
                      HTTPCodec::StreamID id,
                      uint32_t seqNo,
                      // Must be const for gmock
                      const HTTP2PriorityQueue& egressQueue,
                      const WheelTimerInstance& timeout,
                      folly::Optional<HTTPCodec::ExAttributes> exAttributes)
      : MockHTTPTransaction(direction,
                            id,
                            seqNo,
                            egressQueue,
                            timeout.getWheelTimer(),
                            timeout.getDefaultTimeout(),
                            nullptr,
                            false,
                            0,
                            0,
                            http2::DefaultPriority,
                            HTTPCodec::NoStream,
                            exAttributes) {
  }

  MOCK_CONST_METHOD0(extraResponseExpected, bool());

  MOCK_METHOD1(setHandler, void(HTTPTransactionHandler*));

  void setHandlerUnmocked(HTTPTransactionHandler* handler) {
    HTTPTransaction::setHandler(handler);
  }

  MOCK_CONST_METHOD0(canSendHeaders, bool());
  MOCK_METHOD1(sendHeaders, void(const HTTPMessage& headers));
  MOCK_METHOD1(sendHeadersWithEOM, void(const HTTPMessage& headers));
  MOCK_METHOD1(sendBody, void(std::shared_ptr<folly::IOBuf>));
  void sendBody(std::unique_ptr<folly::IOBuf> iob) noexcept override {
    sendBody(std::shared_ptr<folly::IOBuf>(iob.release()));
  }
  MOCK_METHOD1(sendChunkHeader, void(size_t));
  MOCK_METHOD0(sendChunkTerminator, void());
  MOCK_METHOD1(sendTrailers, void(const HTTPHeaders& trailers));
  MOCK_METHOD0(sendEOM, void());
  MOCK_METHOD0(sendAbort, void());
  MOCK_METHOD0(drop, void());
  MOCK_METHOD0(pauseIngress, void());
  MOCK_METHOD0(resumeIngress, void());
  MOCK_CONST_METHOD0(handlerEgressPaused, bool());
  MOCK_METHOD2(newPushedTransaction,
               HTTPTransaction*(HTTPPushTransactionHandler*, ProxygenError*));
  MOCK_METHOD1(setReceiveWindow, void(uint32_t));
  MOCK_CONST_METHOD0(getReceiveWindow, const Window&());

  MOCK_METHOD1(addWaitingForReplaySafety,
               void(folly::AsyncTransport::ReplaySafetyCallback*));
  MOCK_METHOD1(removeWaitingForReplaySafety,
               void(folly::AsyncTransport::ReplaySafetyCallback*));
  MOCK_METHOD2(updateAndSendPriority, void(uint8_t, bool));

  void enablePush() {
    EXPECT_CALL(mockCodec_, supportsPushTransactions())
        .WillRepeatedly(testing::Return(true));
  }

  void setupCodec(CodecProtocol protocol) {
    EXPECT_CALL(mockCodec_, getProtocol())
        .WillRepeatedly(testing::Return(protocol));
  }
  testing::NiceMock<MockHTTPTransactionTransport> mockTransport_;
  const folly::SocketAddress defaultAddress_;
  MockHTTPCodec mockCodec_;
  wangle::TransportInfo setupTransportInfo_;
};

class MockHTTPTransactionTransportCallback
    : public HTTPTransaction::TransportCallback {
 public:
  MockHTTPTransactionTransportCallback() {
  }
  MOCK_METHOD(void, firstHeaderByteFlushed, (), (noexcept));
  MOCK_METHOD(void, firstByteFlushed, (), (noexcept));
  MOCK_METHOD(void, trackedByteFlushed, (), (noexcept));
  MOCK_METHOD(void, lastByteFlushed, (), (noexcept));
  MOCK_METHOD(void, lastByteAcked, (std::chrono::milliseconds), (noexcept));
  MOCK_METHOD(void, trackedByteEventTX, (const ByteEvent&), (noexcept));
  MOCK_METHOD(void, trackedByteEventAck, (const ByteEvent&), (noexcept));
  MOCK_METHOD(void, egressBufferEmpty, (), (noexcept));
  MOCK_METHOD(void, headerBytesGenerated, (HTTPHeaderSize&), (noexcept));
  MOCK_METHOD(void, headerBytesReceived, (const HTTPHeaderSize&), (noexcept));
  MOCK_METHOD(void, bodyBytesGenerated, (size_t), (noexcept));
  MOCK_METHOD(void, bodyBytesReceived, (size_t), (noexcept));
  MOCK_METHOD(void, transportAppRateLimited, (), (noexcept));
};

#if defined(__clang__) && __clang_major__ >= 3 && __clang_minor__ >= 6
#pragma clang diagnostic pop
#endif

} // namespace proxygen
