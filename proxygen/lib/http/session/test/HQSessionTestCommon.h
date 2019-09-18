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

#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
#include <limits>
#include <proxygen/lib/http/codec/HQFramer.h>
#include <proxygen/lib/http/codec/HQUnidirectionalCodec.h>
#include <proxygen/lib/http/session/HQDownstreamSession.h>
#include <proxygen/lib/http/session/HQUpstreamSession.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/MockQuicSocketDriver.h>
#include <proxygen/lib/http/session/test/TestUtils.h>

#define IS_H1Q_FB_V1 (GetParam().alpn_ == "h1q-fb")
#define IS_H1Q_FB_V2 (GetParam().alpn_ == "h1q-fb-v2")
#define IS_HQ (GetParam().alpn_.find("h3") == 0)
#define ALPN_H1Q_FB_V1 (alpn == "h1q-fb")
#define ALPN_H1Q_FB_V2 (alpn == "h1q-fb-v2")
#define ALPN_HQ (alpn.find("h3") == 0)

namespace {
constexpr unsigned int kTransactionTimeout = 500;
constexpr unsigned int kConnectTimeout = 500;
constexpr size_t kQPACKTestDecoderMaxTableSize = 2048;
constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();
const proxygen::hq::PushId kUnknownPushId =
    std::numeric_limits<uint64_t>::max();
constexpr proxygen::hq::PushId kInitialPushId = 12345;
constexpr uint64_t kPushIdIncrement = 1;
constexpr uint64_t kDefaultUnidirStreamCredit = 3;
} // namespace

constexpr uint8_t PR_BODY = 0;
constexpr uint8_t PR_SKIP = 1;

struct PartiallyReliableTestParams {
  std::vector<uint8_t> bodyScript;
};

struct TestParams {
  std::string alpn_;
  bool shouldSendSettings_{true};
  folly::Optional<PartiallyReliableTestParams> prParams;
  uint64_t unidirectionalStreamsCredit{kDefaultUnidirStreamCredit};
  std::size_t numBytesOnPushStream{kUnlimited};
};

std::string prBodyScriptToName(const std::vector<uint8_t>& bodyScript);

size_t encodeQuicIntegerWithAtLeast(uint64_t value,
                                    uint8_t atLeast,
                                    folly::io::QueueAppender& appender);

std::string paramsToTestName(const testing::TestParamInfo<TestParams>& info);

size_t generateStreamPreface(folly::IOBufQueue& writeBuf,
                             proxygen::hq::UnidirectionalStreamType type);

folly::Optional<std::pair<proxygen::hq::UnidirectionalStreamType, size_t>>
parseStreamPreface(folly::io::Cursor cursor, std::string alpn);

void parseReadData(proxygen::hq::HQUnidirectionalCodec* codec,
                   folly::IOBufQueue& readBuf,
                   std::unique_ptr<folly::IOBuf> buf);

void createControlStream(quic::MockQuicSocketDriver* socketDriver,
                         quic::StreamId id,
                         proxygen::hq::UnidirectionalStreamType streamType);

class HQSessionTest
    : public testing::TestWithParam<TestParams>
    , public quic::MockQuicSocketDriver::LocalAppCallback
    , public proxygen::hq::HQUnidirectionalCodec::Callback {

 protected:
  explicit HQSessionTest(
      proxygen::TransportDirection direction,
      folly::Optional<TestParams> overrideParams = folly::none)
      : direction_(direction),
        overrideParams_(overrideParams),
        qpackEncoderCodec_(qpackCodec_, *this),
        qpackDecoderCodec_(qpackCodec_, *this)

  {
    if (direction_ == proxygen::TransportDirection::DOWNSTREAM) {
      hqSession_ = new proxygen::HQDownstreamSession(
          std::chrono::milliseconds(kTransactionTimeout),
          &controllerContainer_.mockController,
          proxygen::mockTransportInfo,
          nullptr,
          nullptr);
      nextUnidirectionalStreamId_ = 2;
    } else if (direction_ == proxygen::TransportDirection::UPSTREAM) {
      hqSession_ = new proxygen::HQUpstreamSession(
          std::chrono::milliseconds(kTransactionTimeout),
          std::chrono::milliseconds(kConnectTimeout),
          &controllerContainer_.mockController,
          proxygen::mockTransportInfo,
          nullptr,
          nullptr);
      nextUnidirectionalStreamId_ = 3;
    } else {
      LOG(FATAL) << "wrong TransportEnum";
    }

    if (!IS_H1Q_FB_V1) {
      egressControlCodec_ = std::make_unique<proxygen::hq::HQControlCodec>(
          nextUnidirectionalStreamId_,
          direction_,
          proxygen::hq::StreamDirection::EGRESS,
          egressSettings_);
    }
    socketDriver_ = std::make_unique<quic::MockQuicSocketDriver>(
        &eventBase_,
        *hqSession_,
        hqSession_->getDispatcher(),
        hqSession_->getDispatcher(),
        direction_ == proxygen::TransportDirection::DOWNSTREAM
            ? quic::MockQuicSocketDriver::TransportEnum::SERVER
            : quic::MockQuicSocketDriver::TransportEnum::CLIENT,
        GetParam().prParams.hasValue());

    hqSession_->setSocket(socketDriver_->getSocket());

    hqSession_->setEgressSettings(egressSettings_.getAllSettings());
    qpackCodec_.setEncoderHeaderTableSize(1024);
    qpackCodec_.setDecoderHeaderTableMaxSize(kQPACKTestDecoderMaxTableSize);
    hqSession_->setInfoCallback(&infoCb_);

    socketDriver_->unidirectionalStreamsCredit_ =
        GetParam().unidirectionalStreamsCredit;

    if (!IS_H1Q_FB_V1) {

      numCtrlStreams_ = (IS_H1Q_FB_V2 ? 1 : (IS_HQ ? 3 : 0));
      socketDriver_->setLocalAppCallback(this);

      if (GetParam().unidirectionalStreamsCredit >= numCtrlStreams_) {
        EXPECT_CALL(infoCb_, onWrite(testing::_, testing::_))
            .Times(testing::AtLeast(numCtrlStreams_));
        EXPECT_CALL(infoCb_, onRead(testing::_, testing::_))
            .Times(testing::AtLeast(numCtrlStreams_));
      }
    }
    quic::QuicSocket::TransportInfo transportInfo = {
        .srtt = std::chrono::microseconds(100),
        .rttvar = std::chrono::microseconds(0),
        .lrtt = std::chrono::microseconds(0),
        .mrtt = std::chrono::microseconds(0),
        .mss = quic::kDefaultUDPSendPacketLen,
        .writableBytes = 0,
        .congestionWindow = 1500,
        .pacingBurstSize = 0,
        .pacingInterval = std::chrono::microseconds(0),
        .packetsRetransmitted = 0,
        .timeoutBasedLoss = 0,
        .pto = std::chrono::microseconds(0),
        .bytesSent = 0,
        .bytesAcked = 0,
        .bytesRecvd = 0,
        .totalBytesRetransmitted = 0,
        .ptoCount = 0,
        .totalPTOCount = 0,
        .largestPacketAckedByPeer = 0,
        .largestPacketSent = 0,
    };
    EXPECT_CALL(*socketDriver_->getSocket(), getTransportInfo())
        .WillRepeatedly(testing::Return(transportInfo));
  }

  bool createControlStreams() {
    // NOTE: this is NOT the stream credit advertised by the peer.
    // this is the number of uni streams that we allow the peer to open. if that
    // is not enough for the control streams, onTransportReady drops the
    // connection, so don't try to create or write to new streams.
    if (GetParam().unidirectionalStreamsCredit < numCtrlStreams_) {
      return false;
    }
    if (IS_H1Q_FB_V2) {
      connControlStreamId_ = nextUnidirectionalStreamId();
      createControlStream(socketDriver_.get(),
                          connControlStreamId_,
                          proxygen::hq::UnidirectionalStreamType::H1Q_CONTROL);
    } else if (IS_HQ) {
      connControlStreamId_ = nextUnidirectionalStreamId();
      createControlStream(socketDriver_.get(),
                          connControlStreamId_,
                          proxygen::hq::UnidirectionalStreamType::CONTROL);
      createControlStream(
          socketDriver_.get(),
          nextUnidirectionalStreamId(),
          proxygen::hq::UnidirectionalStreamType::QPACK_ENCODER);
      createControlStream(
          socketDriver_.get(),
          nextUnidirectionalStreamId(),
          proxygen::hq::UnidirectionalStreamType::QPACK_DECODER);
      if (GetParam().shouldSendSettings_) {
        sendSettings();
      }
    }
    return true;
  }

  void sendSettings() {
    // For H1Q_FB_V2 we call this in some tests, but for V1 it would be an
    // error
    CHECK(!IS_H1Q_FB_V1);
    folly::IOBufQueue writeBuf{folly::IOBufQueue::cacheChainLength()};
    egressControlCodec_->generateSettings(writeBuf);
    socketDriver_->addReadEvent(
        connControlStreamId_, writeBuf.move(), std::chrono::milliseconds(0));
  }

  const std::string getProtocolString() const {
    if (GetParam().alpn_ == "h3") {
      return proxygen::kH3FBCurrentDraft;
    }
    return GetParam().alpn_;
  }

  void readCallback(quic::StreamId id,
                    std::unique_ptr<folly::IOBuf> buf) override {
  }

  void unidirectionalReadCallback(quic::StreamId id,
                                  std::unique_ptr<folly::IOBuf> buf) override {
    // check for control streams
    if (buf->empty()) {
      return;
    }

    auto it = controlStreams_.find(id);
    if (it == controlStreams_.end()) {
      folly::io::Cursor cursor(buf.get());
      auto preface = parseStreamPreface(cursor, getProtocolString());
      CHECK(preface) << "Preface can not be parsed protocolString="
                     << getProtocolString();
      buf->trimStart(preface->second);
      switch (preface->first) {
        case proxygen::hq::UnidirectionalStreamType::H1Q_CONTROL:
        case proxygen::hq::UnidirectionalStreamType::CONTROL:
          ingressControlCodec_ = std::make_unique<proxygen::hq::HQControlCodec>(
              id,
              proxygen::TransportDirection::UPSTREAM,
              proxygen::hq::StreamDirection::INGRESS,
              ingressSettings_,
              preface->first);
          ingressControlCodec_->setCallback(&httpCallbacks_);
          break;
        case proxygen::hq::UnidirectionalStreamType::QPACK_ENCODER:
        case proxygen::hq::UnidirectionalStreamType::QPACK_DECODER:
          break;
        default:
          CHECK(false) << "Unknown stream preface";
      }
      socketDriver_->sock_->setControlStream(id);
      auto res = controlStreams_.emplace(id, preface->first);
      it = res.first;
      if (buf->empty()) {
        return;
      }
    }

    switch (it->second) {
      case proxygen::hq::UnidirectionalStreamType::H1Q_CONTROL:
      case proxygen::hq::UnidirectionalStreamType::CONTROL:
        parseReadData(
            ingressControlCodec_.get(), ingressControlBuf_, std::move(buf));
        break;
      case proxygen::hq::UnidirectionalStreamType::QPACK_ENCODER:
        parseReadData(&qpackEncoderCodec_, encoderReadBuf_, std::move(buf));
        break;
      case proxygen::hq::UnidirectionalStreamType::QPACK_DECODER:
        parseReadData(&qpackDecoderCodec_, decoderReadBuf_, std::move(buf));
        break;
      case proxygen::hq::UnidirectionalStreamType::PUSH:
        CHECK(false) << "Ingress push streams should not go through "
                     << "the unidirectional read path";
        break;
      default:
        CHECK(false) << "Unknown stream type=" << it->second;
    }
  }

  void onError(proxygen::HTTPCodec::StreamID streamID,
               const proxygen::HTTPException& error,
               bool /*newTxn*/) override {
    LOG(FATAL) << __func__ << " streamID=" << streamID
               << " error=" << error.what();
  }

  quic::StreamId nextUnidirectionalStreamId() {
    auto id = nextUnidirectionalStreamId_;
    nextUnidirectionalStreamId_ += 4;
    return id;
  }

  struct MockControllerContainer {
    MockControllerContainer() {
      EXPECT_CALL(mockController, attachSession(testing::_));
      EXPECT_CALL(mockController, detachSession(testing::_));
    }
    testing::StrictMock<proxygen::MockController> mockController;
  };

  testing::StrictMock<proxygen::MockController>& getMockController() {
    return controllerContainer_.mockController;
  }

 public:
  quic::MockQuicSocketDriver* getSocketDriver() {
    return socketDriver_.get();
  }

  proxygen::HQSession* getSession() {
    return hqSession_;
  }

  void setSessionDestroyCallback(
      folly::Function<void(const proxygen::HTTPSessionBase&)> cb) {
    EXPECT_CALL(infoCb_, onDestroy(testing::_))
        .WillOnce(testing::Invoke(
            [&](const proxygen::HTTPSessionBase&) { cb(*hqSession_); }));
  }

  const TestParams& GetParam() const {
    if (overrideParams_) {
      return *overrideParams_;
    } else {
      const testing::TestWithParam<TestParams>* base = this;
      return base->GetParam();
    }
  }

 protected:
  proxygen::TransportDirection direction_;
  folly::Optional<TestParams> overrideParams_;
  // Unidirectional Stream Codecs used for Ingress Only
  proxygen::hq::QPACKEncoderCodec qpackEncoderCodec_;
  proxygen::hq::QPACKDecoderCodec qpackDecoderCodec_;
  // Read/WriteBufs for QPACKCodec, one for the encoder, one for the decoder
  folly::IOBufQueue encoderReadBuf_{folly::IOBufQueue::cacheChainLength()};
  folly::IOBufQueue decoderReadBuf_{folly::IOBufQueue::cacheChainLength()};
  folly::IOBufQueue encoderWriteBuf_{folly::IOBufQueue::cacheChainLength()};
  folly::IOBufQueue decoderWriteBuf_{folly::IOBufQueue::cacheChainLength()};

  folly::EventBase eventBase_;
  proxygen::HQSession* hqSession_;
  MockControllerContainer controllerContainer_;
  std::unique_ptr<quic::MockQuicSocketDriver> socketDriver_;
  folly::SocketAddress localAddress_;
  folly::SocketAddress peerAddress_;
  // One QPACKCodec per session, handles both encoder and decoder
  proxygen::QPACKCodec qpackCodec_;
  std::map<quic::StreamId, proxygen::hq::UnidirectionalStreamType>
      controlStreams_;
  // Ingress Control Stream
  std::unique_ptr<proxygen::hq::HQControlCodec> ingressControlCodec_;
  folly::IOBufQueue ingressControlBuf_{folly::IOBufQueue::cacheChainLength()};
  proxygen::HTTPSettings egressSettings_{
      {proxygen::SettingsId::HEADER_TABLE_SIZE, kQPACKTestDecoderMaxTableSize},
      {proxygen::SettingsId::MAX_HEADER_LIST_SIZE, 655335},
      {proxygen::SettingsId::_HQ_QPACK_BLOCKED_STREAMS, 100}};
  proxygen::HTTPSettings ingressSettings_;
  proxygen::FakeHTTPCodecCallback httpCallbacks_;
  uint8_t numCtrlStreams_{0};
  quic::StreamId connControlStreamId_;
  testing::NiceMock<proxygen::MockHTTPSessionInfoCallback> infoCb_;
  quic::StreamId nextUnidirectionalStreamId_;
  // Egress Control Stream
  std::unique_ptr<proxygen::hq::HQControlCodec> egressControlCodec_;
};
