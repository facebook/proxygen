/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/session/HQSession.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>

namespace proxygen {
class MockConnectCallback : public HQSession::ConnectCallback {
 public:
  MOCK_METHOD0(connectSuccess, void());
  MOCK_METHOD0(onReplaySafe, void());
  MOCK_METHOD1(connectError, void(std::pair<quic::QuicErrorCode, std::string>));
};

class MockHQSession : public HQSession {
 public:
  MockHQSession(const std::chrono::milliseconds transactionsTimeout =
                    std::chrono::milliseconds(5000),
                proxygen::TransportDirection direction =
                    proxygen::TransportDirection::UPSTREAM)
      : HQSession(transactionsTimeout,
                  nullptr,
                  direction,
                  wangle::TransportInfo(),
                  nullptr),
        transactionTimeout_(transactionsTimeout),
        direction_(direction),
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

  bool isDetachable(bool) const override {
    return false;
  }

  void attachThreadLocals(folly::EventBase*,
                          folly::SSLContextPtr,
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

  MOCK_CONST_METHOD0(isReplaySafe, bool());

  MOCK_METHOD1(getTransactionTimeoutHandler,
               HTTPTransaction::Handler*(HTTPTransaction*));

  MOCK_METHOD2(setupOnHeadersComplete, void(HTTPTransaction*, HTTPMessage*));

  GMOCK_METHOD1_(,
                 noexcept,
                 ,
                 onConnectionErrorHandler,
                 void(std::pair<quic::QuicErrorCode, std::string> error));

  MOCK_METHOD1(newTransaction, HTTPTransaction*(HTTPTransaction::Handler*));

  MOCK_METHOD0(drain, void());

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

class MockHqPrUpstreamHTTPHandler : public MockHTTPHandler {
 public:
  MockHqPrUpstreamHTTPHandler() {
  }
  MockHqPrUpstreamHTTPHandler(HTTPTransaction& txn,
                              HTTPMessage* msg,
                              const folly::SocketAddress& addr)
      : MockHTTPHandler(txn, msg, addr) {
  }

  void sendPrHeaders(uint32_t code,
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
    reply.setPartiallyReliable();
    txn_->sendHeaders(reply);
  }

  void expectBodyPeek(
      std::function<void(uint64_t, const folly::IOBufQueue&)> callback =
          std::function<void(uint64_t, const folly::IOBufQueue&)>()) {
    if (callback) {
      EXPECT_CALL(*this, onBodyPeek(testing::_, testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onBodyPeek(testing::_, testing::_));
    }
  }

  void expectBodySkipped(std::function<void(uint64_t)> callback =
                             std::function<void(uint64_t)>()) {
    if (callback) {
      EXPECT_CALL(*this, onBodySkipped(testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onBodySkipped(testing::_));
    }
  }

  void expectBodyRejected(std::function<void(uint64_t)> callback =
                              std::function<void(uint64_t)>()) {
    if (callback) {
      EXPECT_CALL(*this, onBodyRejected(testing::_))
          .WillOnce(testing::Invoke(callback));
    } else {
      EXPECT_CALL(*this, onBodyRejected(testing::_));
    }
  }
};

using MockHqPrDownstreamHTTPHandler = MockHqPrUpstreamHTTPHandler;

class FakeHQHTTPCodecCallback : public FakeHTTPCodecCallback {
 public:
};

} // namespace proxygen
