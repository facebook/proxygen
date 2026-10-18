/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/session/HQSession.h>
#include <proxygen/lib/http/session/HQStreamDispatcher.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>

namespace proxygen {

class MockConnectCallback : public HQSession::ConnectCallback {
 public:
  MOCK_METHOD(void, connectSuccess, ());
  MOCK_METHOD(void, onReplaySafe, ());
  MOCK_METHOD(void, connectError, (quic::QuicError));
  MOCK_METHOD(void, onFirstPeerPacketProcessed, ());
};

class MockHQSession : public HQSession {
 public:
  MockHQSession(const folly::Optional<std::chrono::milliseconds>&
                    transactionsTimeout = folly::none,
                HTTPSessionController* controller = nullptr,
                const folly::Optional<proxygen::TransportDirection>& direction =
                    folly::none)
      : HQSession(transactionsTimeout.value_or(getDefaultTransactionTimeout()),
                  controller,
                  direction.value_or(getMockDefaultDirection()),
                  wangle::TransportInfo(),
                  nullptr),
        transactionTimeout_(
            transactionsTimeout.value_or(getDefaultTransactionTimeout())),
        direction_(direction.value_or(getMockDefaultDirection())),
        quicProtocolInfo_(std::make_shared<QuicProtocolInfo>()),
        quicStreamProtocolInfo_(std::make_shared<QuicStreamProtocolInfo>()) {
    LOG(INFO) << "Creating mock transaction on stream " << lastStreamId_;
    makeMockTransaction(lastStreamId_++);

    ON_CALL(*this, newTransaction(::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SaveArg<0>(&handler_),
            ::testing::WithArgs<0>(
                ::testing::Invoke([&](HTTPTransaction::Handler* handler) {
                  CHECK(txn_);
                  LOG(INFO) << "Setting transaction handler to " << handler;
                  txn_->HTTPTransaction::setHandler(handler);
                })),
            ::testing::Return(txn_.get())));
  }

  static std::chrono::milliseconds getDefaultTransactionTimeout() {
    return std::chrono::milliseconds(5000);
  }

  static proxygen::TransportDirection getMockDefaultDirection() {
    return proxygen::TransportDirection::UPSTREAM;
  }

  bool isDetachable(bool) const override {
    return false;
  }

  void attachThreadLocals(folly::EventBase*,
                          std::shared_ptr<const folly::SSLContext>,
                          const WheelTimerInstance&,
                          HTTPSessionStats*,
                          FilterIteratorFn,
                          HeaderCodec::Stats*,
                          HTTPSessionController*) override {
  }

  void detachThreadLocals(bool) override {
  }

  void onHeadersComplete(HTTPCodec::StreamID streamID,
                         std::unique_ptr<HTTPMessage> msg,
                         bool eom = false) {
    if (handler_) {
      handler_->onHeadersComplete(std::move(msg));
      if (eom) {
        handler_->onEOM();
      }
    }
  };

  void onHeadersComplete(HTTPCodec::StreamID streamID,
                         int statusCode,
                         const std::string& statusMessage,
                         bool eom = false) {
    auto resp = std::make_unique<HTTPMessage>();
    resp->setStatusCode(statusCode);
    resp->setStatusMessage(statusMessage);
    onHeadersComplete(streamID, std::move(resp), eom);
  }

  MOCK_METHOD(bool, isReplaySafe, (), (const));

  MOCK_METHOD(HTTPTransaction::Handler*,
              getTransactionTimeoutHandler,
              (HTTPTransaction*));

  MOCK_METHOD(void, setupOnHeadersComplete, (HTTPTransaction*, HTTPMessage*));

  MOCK_METHOD((void),
              onConnectionSetupErrorHandler,
              (quic::QuicError),
              (noexcept));

  MOCK_METHOD(HTTPTransaction*, newTransaction, (HTTPTransaction::Handler*));

  MOCK_METHOD(void, drain, ());

  MOCK_CONST_METHOD0(getQuicSocket, quic::QuicSocket*());

  MockHTTPTransaction* makeMockTransaction(HTTPCodec::StreamID id) {
    LOG(INFO) << "Creating mocked transaction on stream " << id;

    txn_ = std::make_unique<::testing::StrictMock<MockHTTPTransaction>>(
        direction_,
        id,
        0, /* seqNo */
        egressQueue_,
        nullptr, /* timer */
        transactionTimeout_);

    LOG(INFO) << "Setting default handlers on the new transaction "
              << txn_.get();

    EXPECT_CALL(*txn_, setHandler(::testing::_))
        .WillRepeatedly(
            ::testing::Invoke([txn = txn_.get()](HTTPTransactionHandler* hdlr) {
              LOG(INFO) << "Setting handler on " << txn << " to " << hdlr;
              txn->HTTPTransaction::setHandler(hdlr);
            }));

    EXPECT_CALL(*txn_, canSendHeaders())
        .WillRepeatedly(::testing::Invoke([txn = txn_.get()] {
          return txn->HTTPTransaction::canSendHeaders();
        }));

    EXPECT_CALL(txn_->mockTransport_, getCurrentTransportInfo(::testing::_))
        .WillRepeatedly(::testing::DoAll(
            ::testing::WithArgs<0>(
                ::testing::Invoke([&](wangle::TransportInfo* tinfo) {
                  if (tinfo) {
                    tinfo->protocolInfo = quicStreamProtocolInfo_;
                  }
                })),
            ::testing::Return(true)));

    LOG(INFO) << "Returning the new mocked transaction " << txn_.get();

    return txn_.get();
  }

  bool erasePushStream(quic::StreamId) override {
    return false;
  }
  uint32_t getNumOutgoingStreams() const override {
    return static_cast<uint32_t>(streams_.size());
  }
  uint32_t getNumIncomingStreams() const override {
    return static_cast<uint32_t>(streams_.size());
  }

  void dispatchPushStream(quic::StreamId /* streamId */,
                          hq::PushId /* pushId */,
                          size_t /* to consume */) override {
  }

  const std::chrono::milliseconds transactionTimeout_;
  const proxygen::TransportDirection direction_;

  HTTP2PriorityQueue egressQueue_;
  wangle::TransportInfo currentTransportInfo_;
  std::shared_ptr<QuicProtocolInfo> quicProtocolInfo_;
  std::shared_ptr<QuicStreamProtocolInfo> quicStreamProtocolInfo_;

  std::unique_ptr<::testing::StrictMock<MockHTTPTransactionTransport>>
      transport_;

  std::unique_ptr<::testing::StrictMock<MockHTTPTransaction>> txn_;

  HTTPCodec::StreamID lastStreamId_{1}; // streamID 0 is reserved
  HTTPTransaction::Handler* handler_;
};

class FakeHQHTTPCodecCallback : public FakeHTTPCodecCallback {
 public:
};

} // namespace proxygen
