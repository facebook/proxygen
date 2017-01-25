/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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

class MockHTTPTransactionTransport: public HTTPTransaction::Transport {
 public:
  MockHTTPTransactionTransport() {}
  GMOCK_METHOD1_(, noexcept,, pauseIngress, void(HTTPTransaction*));
  GMOCK_METHOD1_(, noexcept,, resumeIngress, void(HTTPTransaction*));
  GMOCK_METHOD1_(, noexcept,, transactionTimeout, void(HTTPTransaction*));
  GMOCK_METHOD4_(, noexcept,, sendHeaders, void(HTTPTransaction*,
                                                 const HTTPMessage&,
                                                 HTTPHeaderSize*,
                                                 bool));
  GMOCK_METHOD3_(, noexcept,, sendBody,
                 size_t(HTTPTransaction*, std::shared_ptr<folly::IOBuf>, bool));

  size_t sendBody(HTTPTransaction* txn, std::unique_ptr<folly::IOBuf> iob,
                  bool eom) noexcept override {
    return sendBody(txn, std::shared_ptr<folly::IOBuf>(iob.release()), eom);
  }

  GMOCK_METHOD2_(, noexcept,, sendChunkHeader, size_t(HTTPTransaction*,
                                                       size_t));
  GMOCK_METHOD1_(, noexcept,, sendChunkTerminator, size_t(HTTPTransaction*));
  GMOCK_METHOD2_(, noexcept,, sendTrailers, size_t(HTTPTransaction*,
                                                    const HTTPHeaders&));
  GMOCK_METHOD1_(, noexcept,, sendEOM, size_t(HTTPTransaction*));
  GMOCK_METHOD2_(, noexcept,, sendAbort, size_t(HTTPTransaction*,
                                                 ErrorCode));
  GMOCK_METHOD2_(, noexcept,, sendPriority,
                 size_t(HTTPTransaction*, const http2::PriorityUpdate&));
  GMOCK_METHOD0_(, noexcept,, notifyPendingEgress, void());
  GMOCK_METHOD1_(, noexcept,, detach, void(HTTPTransaction*));
  GMOCK_METHOD2_(, noexcept,, sendWindowUpdate, size_t(HTTPTransaction*,
                                                        uint32_t));
  GMOCK_METHOD1_(, noexcept,, notifyIngressBodyProcessed, void(uint32_t));
  GMOCK_METHOD1_(, noexcept,, notifyEgressBodyBuffered, void(int64_t));
  GMOCK_METHOD0_(, noexcept,, getLocalAddressNonConst,
                 const folly::SocketAddress&());
  GMOCK_METHOD2_(, noexcept,, newPushedTransaction,
                 HTTPTransaction*(HTTPCodec::StreamID assocStreamId,
                                  HTTPTransaction::PushHandler* handler));
  const folly::SocketAddress& getLocalAddress()
    const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
      ->getLocalAddressNonConst();
  }
  GMOCK_METHOD0_(, noexcept,, getPeerAddressNonConst,
                 const folly::SocketAddress&());
  const folly::SocketAddress& getPeerAddress()
    const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
      ->getPeerAddressNonConst();
  }
  MOCK_CONST_METHOD1(describe, void(std::ostream&));
  GMOCK_METHOD0_(, noexcept,, getSetupTransportInfoNonConst,
                 const wangle::TransportInfo&());
  const wangle::TransportInfo& getSetupTransportInfo() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
      ->getSetupTransportInfoNonConst();
  }

  MOCK_METHOD1(getCurrentTransportInfo, bool(wangle::TransportInfo*));
  GMOCK_METHOD0_(, noexcept,, getCodecNonConst, const HTTPCodec&());
  const HTTPCodec& getCodec() const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
      ->getCodecNonConst();
  }
  MOCK_CONST_METHOD0(isDraining, bool());
  MOCK_CONST_METHOD0(getSecurityProtocol, std::string());

  MOCK_CONST_METHOD0(getTransport, const folly::AsyncTransportWrapper*());
  MOCK_METHOD0(getTransport, folly::AsyncTransportWrapper*());

  GMOCK_METHOD1_(, noexcept,, addWaitingForReplaySafety,
      void(folly::AsyncTransport::ReplaySafetyCallback*));
  GMOCK_METHOD1_(, noexcept,, removeWaitingForReplaySafety,
      void(folly::AsyncTransport::ReplaySafetyCallback*));
  MOCK_CONST_METHOD0(getNumWaitingForReplaySafety, size_t());

  GMOCK_METHOD0_(, noexcept,, getUnderlyingTransportNonConst,
      const folly::AsyncTransportWrapper*());
  const folly::AsyncTransportWrapper* getUnderlyingTransport()
      const noexcept override {
    return const_cast<MockHTTPTransactionTransport*>(this)
      ->getUnderlyingTransport();
  }
};

class MockHTTPTransaction : public HTTPTransaction {
 public:
  MockHTTPTransaction(TransportDirection direction,
                      HTTPCodec::StreamID id,
                      uint32_t seqNo,
                      // Must be const for gmock
                      const HTTP2PriorityQueue& egressQueue,
                      const WheelTimerInstance& timeout,
                      HTTPSessionStats* stats = nullptr,
                      bool useFlowControl = false,
                      uint32_t receiveInitialWindowSize = 0,
                      uint32_t sendInitialWindowSize = 0,
                      http2::PriorityUpdate priority = http2::DefaultPriority) :
      HTTPTransaction(direction, id, seqNo, mockTransport_,
                      const_cast<HTTP2PriorityQueue&>(egressQueue),
                      timeout, stats, useFlowControl,
                      receiveInitialWindowSize,
                      sendInitialWindowSize,
                      priority),
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
      .WillByDefault(testing::Invoke([this] (HTTPTransactionHandler* handler) {
            this->setHandlerUnmocked(handler);
        }));

    // By default we expect canSendHeaders in test to return true
    // Tests that specifically require canSendHeaders to return false need
    // to set the behavior locally.  This is due to the fact that the mocked
    // methods below imply internal state is not correctly tracked/managed
    // in the context of tests
    ON_CALL(*this, canSendHeaders()).WillByDefault(testing::Return(true));
  }

  MOCK_CONST_METHOD0(extraResponseExpected, bool());

  MOCK_METHOD1(setHandler, void(HTTPTransactionHandler*));

  void setHandlerUnmocked(HTTPTransactionHandler* handler) {
    HTTPTransaction::setHandler(handler);
  }

  MOCK_CONST_METHOD0(canSendHeaders, bool());
  MOCK_METHOD1(sendHeaders, void(const HTTPMessage& headers));
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
  MOCK_METHOD1(newPushedTransaction,
               HTTPTransaction*(HTTPPushTransactionHandler*));
  MOCK_METHOD1(setReceiveWindow, void(uint32_t));
  MOCK_CONST_METHOD0(getReceiveWindow, const Window&());

  MOCK_METHOD1(addWaitingForReplaySafety,
      void(folly::AsyncTransport::ReplaySafetyCallback*));
  MOCK_METHOD1(removeWaitingForReplaySafety,
      void(folly::AsyncTransport::ReplaySafetyCallback*));

  void enablePush() {
    EXPECT_CALL(mockCodec_, supportsPushTransactions())
      .WillRepeatedly(testing::Return(true));
  }

  testing::NiceMock<MockHTTPTransactionTransport> mockTransport_;
  const folly::SocketAddress defaultAddress_;
  MockHTTPCodec mockCodec_;
  wangle::TransportInfo setupTransportInfo_;
};

class MockHTTPTransactionTransportCallback:
      public HTTPTransaction::TransportCallback {
 public:
  MockHTTPTransactionTransportCallback() {}
  GMOCK_METHOD0_(, noexcept,, firstByteFlushed, void());
  GMOCK_METHOD1_(, noexcept,, headerBytesGenerated, void(HTTPHeaderSize&));
  GMOCK_METHOD1_(, noexcept,, bodyBytesGenerated, void(size_t));
};

#if defined(__clang__) && __clang_major__ >= 3 && __clang_minor__ >= 6
#pragma clang diagnostic pop
#endif

}
