/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HQSession.h>

#include <proxygen/lib/http/codec/HQControlCodec.h>
#include <proxygen/lib/http/codec/HQStreamCodec.h>
#include <proxygen/lib/http/codec/HQUtils.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPCodecFilter.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

#include <boost/cast.hpp>
#include <folly/CppAttributes.h>
#include <folly/ScopeGuard.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestructionBase.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>
#include <wangle/acceptor/ConnectionManager.h>

namespace {
static const uint16_t kMaxReadsPerLoop = 16;
static const std::string kNoProtocolString("");
static const std::string kH1QV1ProtocolString("h1q-fb");
static const std::string kH1QLigerProtocolString("h1q");
static const std::string kH1QV2ProtocolString("h1q-fb-v2");
static const std::string kQUICProtocolName("QUIC");

// handleSessionError is mostly setup to process application error codes
// that we want to send.  If we receive an application error code, convert to
// HTTP_CLOSED_CRITICAL_STREAM
quic::QuicErrorCode quicControlStreamError(quic::QuicErrorCode error) {
  return folly::variant_match(
      error,
      [&](quic::ApplicationErrorCode) {
        return quic::QuicErrorCode(
            proxygen::HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM);
      },
      [](quic::LocalErrorCode errorCode) {
        return quic::QuicErrorCode(errorCode);
      },
      [](quic::TransportErrorCode errorCode) {
        return quic::QuicErrorCode(errorCode);
      });
}

} // namespace

using namespace proxygen::hq;

namespace proxygen {

const std::string kH3FBCurrentDraft("h3-fb-05");
const std::string kH3CurrentDraft("h3-20");
const std::string kHQCurrentDraft("hq-20");

const http2::PriorityUpdate hqDefaultPriority{kSessionStreamId, false, 15};

HQSession::~HQSession() {
  VLOG(3) << *this << " closing";
  CHECK(streams_.empty());
  runDestroyCallbacks();
}

void HQSession::setSessionStats(HTTPSessionStats* stats) {
  HTTPSessionBase::setSessionStats(stats);
  for (auto& it : streams_) {
    it.second.byteEventTracker_.setTTLBAStats(stats);
  }
}

void HQSession::onNewBidirectionalStream(quic::StreamId id) noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": new streamID=" << id;
  // The transport should never call onNewBidirectionalStream before
  // onTransportReady
  DCHECK(versionUtils_) << "The transport should never call " << __func__
                        << " before onTransportReady";
  if (!versionUtils_->checkNewStream(id)) {
    return;
  }
  auto hqStream = findNonDetachedStream(id);
  DCHECK(!hqStream);
  hqStream = createStreamTransport(id);
  DCHECK(hqStream);
  sock_->setReadCallback(id, this);
  maxIncomingStreamId_ = std::max(maxIncomingStreamId_, id);
}

void HQSession::onNewUnidirectionalStream(quic::StreamId id) noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": new streamID=" << id;
  // The transport should never call onNewUnidirectionalStream
  // before onTransportReady
  DCHECK(versionUtils_) << "The transport should never call " << __func__
                        << " before onTransportReady";
  if (!versionUtils_->checkNewStream(id)) {
    return;
  }

  auto ctrlStream = findControlStream(id);
  DCHECK(!ctrlStream) << "duplicate " << __func__ << " for streamID=" << id;
  // This has to be a new control or push stream, but we haven't read the
  // preface yet
  sock_->setPeekCallback(id, this);
}

void HQSession::onStopSending(quic::StreamId id,
                              quic::ApplicationErrorCode error) noexcept {
  auto errorCode = static_cast<HTTP3::ErrorCode>(error);
  VLOG(3) << __func__ << " sess=" << *this << ": new streamID=" << id
          << " error=" << toString(errorCode);
  auto stream = findStream(id);
  if (stream) {
    handleWriteError(stream, error);
  }
}

bool HQSession::H1QFBV1VersionUtils::checkNewStream(quic::StreamId id) {
  // Reject all unidirectional streams and all server-initiated streams
  if (session_.sock_->isUnidirectionalStream(id) ||
      session_.sock_->isServerStream(id)) {
    session_.abortStream(HTTPException::Direction::INGRESS_AND_EGRESS,
                         id,
                         HTTP3::ErrorCode::HTTP_WRONG_STREAM);
    QUIC_TRACE_SOCK(stream_event, session_.sock_, "abort", id, 0);
    return false;
  }
  return true;
}

bool HQSession::GoawayUtils::checkNewStream(HQSession& session,
                                            quic::StreamId id) {
  // Reject all bidirectional, server-initiated streams
  if (session.sock_->isBidirectionalStream(id) &&
      session.sock_->isServerStream(id)) {
    session.abortStream(HTTPException::Direction::INGRESS_AND_EGRESS,
                        id,
                        HTTP3::ErrorCode::HTTP_WRONG_STREAM);
    QUIC_TRACE_SOCK(stream_event, session.sock_, "abort", id, 0);
    return false;
  }
  // Cancel any stream that is out of the range allowed by GOAWAY
  if (session.drainState_ != DrainState::NONE) {
    // TODO: change this in (id >= maxAllowedStreamId_)
    // (see https://github.com/quicwg/base-drafts/issues/1717)
    // NOTE: need to consider the downstream case as well, since streams may
    // come out of order and we may get a new stream with lower id than
    // advertised in the goaway, and we need to accept that
    if ((session.direction_ == TransportDirection::UPSTREAM &&
         id > session.maxAllowedStreamId_) ||
        (session.direction_ == TransportDirection::DOWNSTREAM &&
         session.sock_->isBidirectionalStream(id) &&
         id > session.maxIncomingStreamId_)) {
      session.abortStream(HTTPException::Direction::INGRESS_AND_EGRESS,
                          id,
                          HTTP3::ErrorCode::HTTP_REQUEST_REJECTED);
      QUIC_TRACE_SOCK(stream_event, session.sock_, "abort", id, 0);
      return false;
    }
  }

  return true;
}

bool HQSession::onTransportReadyCommon() noexcept {
  localAddr_ = sock_->getLocalAddress();
  peerAddr_ = sock_->getPeerAddress();
  quicInfo_->clientConnectionId = sock_->getClientConnectionId();
  quicInfo_->serverConnectionId = sock_->getServerConnectionId();
  // NOTE: this can drop the connection if the next protocol is not supported
  if (!getAndCheckApplicationProtocol()) {
    return false;
  }
  transportInfo_.acceptTime = getCurrentTime();
  getCurrentTransportInfoWithoutUpdate(&transportInfo_);
  transportInfo_.setupTime = millisecondsSince(transportStart_);
  transportInfo_.connectLatency = millisecondsSince(transportStart_).count();
  transportInfo_.protocolInfo = quicInfo_;
  if (!versionUtils_->createEgressControlStreams()) {
    return false;
  }
  // Apply the default settings
  // TODO: 0-RTT settings
  versionUtils_->applySettings({});
  // notifyPendingShutdown may be invoked before onTransportReady,
  // so we need to address that here by kicking the GOAWAY logic if needed
  if (drainState_ == DrainState::PENDING) {
    versionUtils_->sendGoaway();
  }
  return true;
}

bool HQSession::H1QFBV2VersionUtils::createEgressControlStreams() {
  if (!session_.createEgressControlStream(
          UnidirectionalStreamType::H1Q_CONTROL)) {
    return false;
  }
  session_.scheduleWrite();
  return true;
}

bool HQSession::HQVersionUtils::createEgressControlStreams() {
  if (!session_.createEgressControlStream(UnidirectionalStreamType::CONTROL) ||
      !session_.createEgressControlStream(
          UnidirectionalStreamType::QPACK_ENCODER) ||
      !session_.createEgressControlStream(
          UnidirectionalStreamType::QPACK_DECODER)) {
    return false;
  }

  session_.sendSettings();
  session_.scheduleWrite();
  return true;
}

bool HQSession::createEgressControlStream(UnidirectionalStreamType streamType) {
  auto id = sock_->createUnidirectionalStream();
  if (id.hasError()) {
    LOG(ERROR) << "Failed to create " << streamType
               << " unidirectional stream. error='" << id.error() << "'";
    onConnectionError(std::make_pair(quic::LocalErrorCode::CONNECT_FAILED,
                                     "Failed to create unidirectional stream"));
    return false;
  }

  auto matchPair = controlStreams_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(streamType),
      std::forward_as_tuple(*this, id.value(), streamType));
  CHECK(matchPair.second) << "Emplacement failed";
  sock_->setControlStream(id.value());
  matchPair.first->second.generateStreamPreface();
  return true;
}

size_t HQSession::HQControlStream::generateStreamPreface() {
  VLOG(4) << "generating stream preface for " << type_
          << " stream streamID=" << getEgressStreamId() << " sess=" << session_;
  folly::io::QueueAppender appender(&writeBuf_, sizeof(uint64_t));
  auto res =
      quic::encodeQuicInteger(static_cast<hq::StreamTypeType>(type_), appender);
  CHECK(!res.hasError());
  return res.value();
}

HQSession::HQControlStream* FOLLY_NULLABLE
HQSession::createIngressControlStream(quic::StreamId id,
                                      UnidirectionalStreamType streamType) {

  auto ctrlStream = findControlStream(streamType);
  // this is an error in the use of the API, egress control streams must get
  // created at the very beginning
  if (!ctrlStream) {
    LOG(FATAL) << "Cannot create ingress control stream without an egress "
                  "stream streamID="
               << id << " sess=" << *this;
    return nullptr;
  }

  if (ctrlStream->ingressCodec_) {
    LOG(ERROR) << "Too many " << streamType << " streams for sess=" << *this;
    dropConnectionWithError(
        std::make_pair(
            HTTP3::ErrorCode::HTTP_WRONG_STREAM_COUNT,
            "HTTP wrong stream count"),
        kErrorConnection);
    return nullptr;
  }

  ctrlStream->setIngressStreamId(id);
  ctrlStream->setIngressCodec(
      versionUtils_->createControlCodec(streamType, *ctrlStream));
  return ctrlStream;
}

std::unique_ptr<hq::HQUnidirectionalCodec>
HQSession::H1QFBV2VersionUtils::createControlCodec(
    hq::UnidirectionalStreamType type, HQControlStream& controlStream) {
  switch (type) {
    case hq::UnidirectionalStreamType::H1Q_CONTROL: {
      auto codec = std::make_unique<hq::HQControlCodec>(
          controlStream.getIngressStreamId(),
          session_.direction_,
          hq::StreamDirection::INGRESS,
          session_.ingressSettings_,
          type);
      codec->setCallback(&controlStream);
      return codec;
    }
    default:
      CHECK(false);
      return nullptr;
  }
}

std::unique_ptr<hq::HQUnidirectionalCodec>
HQSession::HQVersionUtils::createControlCodec(hq::UnidirectionalStreamType type,
                                              HQControlStream& controlStream) {
  switch (type) {
    case hq::UnidirectionalStreamType::CONTROL: {
      auto codec = std::make_unique<hq::HQControlCodec>(
          controlStream.getIngressStreamId(),
          session_.direction_,
          hq::StreamDirection::INGRESS,
          session_.ingressSettings_,
          type);
      codec->setCallback(&controlStream);
      return codec;
    }
    // This is quite weird for now. The stream types are defined  based on
    // the component that initiates them, so the ingress stream from the
    // QPACK Encoder is linked to the local QPACKDecoder, and viceversa
    case hq::UnidirectionalStreamType::QPACK_ENCODER:
      return std::make_unique<hq::QPACKEncoderCodec>(qpackCodec_,
                                                     controlStream);
    case hq::UnidirectionalStreamType::QPACK_DECODER:
      return std::make_unique<hq::QPACKDecoderCodec>(qpackCodec_,
                                                     controlStream);
    default:
      LOG(FATAL) << "Failed to create ingress codec";
      return nullptr;
  }
}

bool HQSession::getAndCheckApplicationProtocol() {
  CHECK(sock_);
  auto alpn = sock_->getAppProtocol();
  if (alpn) {
    if (alpn == kH1QV1ProtocolString || alpn == kH1QLigerProtocolString ||
        alpn == kHQCurrentDraft) {
      version_ = HQVersion::H1Q_FB_V1;
    } else if (alpn == kH1QV2ProtocolString) {
      version_ = HQVersion::H1Q_FB_V2;
    } else if (alpn == kH3FBCurrentDraft || alpn == kH3CurrentDraft) {
      version_ = HQVersion::HQ;
    }
  }
  if (!alpn || !version_) {
    // next protocol not specified or version not supported, close connection
    // with error
    LOG(ERROR) << "next protocol not supported: "
               << (alpn ? *alpn : "no protocol") << " sess=" << *this;

    onConnectionError(std::make_pair(quic::LocalErrorCode::CONNECT_FAILED,
                                     "ALPN not supported"));
    return false;
  }
  alpn_ = *alpn;
  setVersionUtils();
  return true;
}

void HQSession::setVersionUtils() {
  DCHECK(version_);
  switch (*version_) {
    case HQVersion::H1Q_FB_V1:
      versionUtils_ = std::make_unique<H1QFBV1VersionUtils>(*this);
      break;
    case HQVersion::H1Q_FB_V2:
      versionUtils_ = std::make_unique<H1QFBV2VersionUtils>(*this);
      break;
    case HQVersion::HQ:
      versionUtils_ = std::make_unique<HQVersionUtils>(*this);
      break;
    default:
      LOG(FATAL) << "No Version Utils for version "
                 << alpn_;
  }
  versionUtilsReady_.set();
}

void HQSession::onReplaySafe() noexcept {
  // We might have got onTransportReady with 0-rtt in which case we only get the
  // server connection id after replay safe.
  quicInfo_->serverConnectionId = sock_->getServerConnectionId();
  if (infoCallback_) {
    infoCallback_->onFullHandshakeCompletion(*this);
  }

  for (auto callback : waitingForReplaySafety_) {
    callback->onReplaySafe();
  }
  waitingForReplaySafety_.clear();
}

void HQSession::onConnectionError(
    std::pair<quic::QuicErrorCode, std::string> code) noexcept {
  // the connector will drop the connection in case of connect error
  HQSession::DestructorGuard dg(this);
  VLOG(4) << __func__ << " sess=" << *this
          << ": connection error=" << code.second;

  // Map application errors here to kErrorConnectionReset: eg, the peer tore
  // down the connection
  auto proxygenErr = toProxygenError(code.first, /*fromPeer=*/true);
  if (infoCallback_) {
    infoCallback_->onIngressError(*this, proxygenErr);
  }

  onConnectionErrorHandler(code);

  // force close all streams.
  // close with error won't invoke any connection callback, reentrancy safe
  dropConnectionWithError(std::move(code), proxygenErr);
}

bool HQSession::getCurrentTransportInfo(wangle::TransportInfo* tinfo) {
  getCurrentTransportInfoWithoutUpdate(tinfo);
  tinfo->setupTime = transportInfo_.setupTime;
  tinfo->secure = transportInfo_.secure;
  tinfo->appProtocol = transportInfo_.appProtocol;
  tinfo->connectLatency = transportInfo_.connectLatency;
  // Copy props from the transport info.
  transportInfo_.rtt = tinfo->rtt;
  transportInfo_.rtt_var = tinfo->rtt_var;
  if (sock_) {
    auto quicInfo = sock_->getTransportInfo();
    quicInfo_->ptoCount = quicInfo.ptoCount;
    quicInfo_->totalPTOCount = quicInfo.totalPTOCount;
    quicInfo_->totalTransportBytesSent = quicInfo.bytesSent;
    quicInfo_->totalTransportBytesRecvd = quicInfo.bytesRecvd;
  }
  return true;
}

bool HQSession::getCurrentTransportInfoWithoutUpdate(
    wangle::TransportInfo* tinfo) const {
  tinfo->validTcpinfo = true;
  tinfo->appProtocol =
      std::make_shared<std::string>(alpn_);
  tinfo->securityType = kQUICProtocolName;
  tinfo->protocolInfo = quicInfo_;
  if (sock_) {
    auto quicInfo = sock_->getTransportInfo();
    tinfo->rtt = quicInfo.srtt;
    tinfo->rtt_var = static_cast<int64_t>(quicInfo.rttvar.count());
    // Cwnd is logged in terms of MSS.
    // TODO: this is incorrect if Quic negotiates a different mss.
    tinfo->cwnd = static_cast<int64_t>(quicInfo.congestionWindow /
                                       quic::kDefaultUDPSendPacketLen);
    tinfo->cwndBytes = static_cast<int64_t>(quicInfo.congestionWindow);
    tinfo->rtx = static_cast<int64_t>(quicInfo.packetsRetransmitted);
    tinfo->rtx_tm = static_cast<int64_t>(quicInfo.timeoutBasedLoss);
    tinfo->rto = static_cast<int64_t>(quicInfo.pto.count());
    tinfo->totalBytes = static_cast<int64_t>(quicInfo.bytesSent);
  }
  // TODO: fill up other properties.
  return true;
}

bool HQSession::getCurrentStreamTransportInfo(QuicStreamProtocolInfo* qspinfo,
                                              quic::StreamId streamId) {
  if (sock_) {
    auto streamTransportInfo = sock_->getStreamTransportInfo(streamId);
    if (streamTransportInfo) {
      qspinfo->streamTransportInfo = streamTransportInfo.value();
      return true;
    }
  }
  return false;
}

void HQSession::HQStreamTransportBase::generateGoaway() {
  folly::IOBufQueue dummyBuf{folly::IOBufQueue::cacheChainLength()};
  if (!codecStreamId_) {
    codecStreamId_ = 0;
  }
  auto g = folly::makeGuard(setActiveCodec(__func__));
  if (codecFilterChain->isReusable() || codecFilterChain->isWaitingToDrain()) {
    codecFilterChain->generateGoaway(
        dummyBuf, *codecStreamId_, ErrorCode::NO_ERROR);
  }
}

bool HQSession::HQStreamTransportBase::hasPendingBody() const {
  return writeBuf_.chainLength() != 0 ||
         (queueHandle_.isTransactionEnqueued() && txn_.hasPendingBody());
}

bool HQSession::HQStreamTransportBase::hasPendingEOM() const {
  return pendingEOM_ ||
         (queueHandle_.isTransactionEnqueued() && txn_.isEgressEOMQueued());
}

bool HQSession::HQStreamTransportBase::hasPendingEgress() const {
  return writeBuf_.chainLength() > 0 || pendingEOM_ ||
         queueHandle_.isTransactionEnqueued();
}

bool HQSession::HQStreamTransportBase::wantsOnWriteReady(size_t canSend) const {
  // The txn wants onWriteReady if it's enqueued AND
  //   a) There is available flow control and it has body OR
  //   b) All body is egressed and it has only pending EOM
  return queueHandle_.isTransactionEnqueued() &&
         ((canSend > writeBuf_.chainLength() && txn_.hasPendingBody()) ||
          (!txn_.hasPendingBody() && txn_.isEgressEOMQueued()));
}

void HQSession::drainImpl() {
  if (drainState_ != DrainState::NONE) {
    // no op
    VLOG(5) << "Already draining sess=" << *this;
    return;
  }
  drainState_ = DrainState::PENDING;
  if (versionUtils_) {
    versionUtils_->sendGoaway();
  }
  setCloseReason(ConnectionCloseReason::SHUTDOWN);
}

void HQSession::H1QFBV1VersionUtils::sendGoaway() {
  for (auto& it : session_.streams_) {
    it.second.generateGoaway();
  }
}

void HQSession::GoawayUtils::sendGoaway(HQSession& session) {
  if (session.direction_ == TransportDirection::UPSTREAM) {
    return;
  }
  if (session.drainState_ == DrainState::DONE) {
    return;
  }
  // send GOAWAY frame on the control stream
  DCHECK(session.drainState_ == DrainState::PENDING ||
         session.drainState_ == DrainState::FIRST_GOAWAY);

  auto connCtrlStream =
      session.findControlStream((session.version_ == HQVersion::H1Q_FB_V2)
                                    ? UnidirectionalStreamType::H1Q_CONTROL
                                    : UnidirectionalStreamType::CONTROL);
  auto g = folly::makeGuard(connCtrlStream->setActiveCodec(__func__));
  // cannot get here before onTransportReady, since the VersionUtils are being
  // set after ALPN is available
  DCHECK(connCtrlStream);
  auto goawayStreamId = session.getGoawayStreamId();
  auto generated = connCtrlStream->codecFilterChain->generateGoaway(
      connCtrlStream->writeBuf_, goawayStreamId, ErrorCode::NO_ERROR);
  auto writeOffset =
      session.sock_->getStreamWriteOffset(connCtrlStream->getEgressStreamId());
  auto writeBufferedBytes = session.sock_->getStreamWriteBufferedBytes(
      connCtrlStream->getEgressStreamId());
  if (generated == 0 || writeOffset.hasError() ||
      writeBufferedBytes.hasError()) {
    // shortcut to shutdown
    LOG(ERROR) << " error generating GOAWAY sess=" << session;
    session.drainState_ = DrainState::DONE;
    return;
  }
  VLOG(3) << "generated GOAWAY maxStreamID=" << goawayStreamId
          << " sess=" << session;

  auto res = session.sock_->registerDeliveryCallback(
      connCtrlStream->getEgressStreamId(),
      *writeOffset + *writeBufferedBytes +
          connCtrlStream->writeBuf_.chainLength(),
      connCtrlStream);
  if (res.hasError()) {
    // shortcut to shutdown
    LOG(ERROR) << " error generating GOAWAY sess=" << session;
    session.drainState_ = DrainState::DONE;
    return;
  }
  session.scheduleWrite();
  if (session.drainState_ == DrainState::PENDING) {
    session.drainState_ = DrainState::FIRST_GOAWAY;
  } else {
    DCHECK_EQ(session.drainState_, DrainState::FIRST_GOAWAY);
    session.drainState_ = DrainState::SECOND_GOAWAY;
  }
}

quic::StreamId HQSession::getGoawayStreamId() {
  if (drainState_ == DrainState::NONE || drainState_ == DrainState::PENDING) {
    // The maximum representable stream id in a quic varint
    return quic::kEightByteLimit;
  }
  return maxIncomingStreamId_;
}

size_t HQSession::sendSettings() {
  DCHECK(versionUtils_) << "The transport should never call " << __func__
                        << " before onTransportReady";
  return versionUtils_->sendSettings();
}

size_t HQSession::HQVersionUtils::sendSettings() {
  for (auto& setting : session_.egressSettings_.getAllSettings()) {
    auto id = httpToHqSettingsId(setting.id);
    if (id) {
      switch (*id) {
        case hq::SettingId::HEADER_TABLE_SIZE:
          qpackCodec_.setDecoderHeaderTableMaxSize(setting.value);
          break;
        case hq::SettingId::QPACK_BLOCKED_STREAMS:
          qpackCodec_.setMaxBlocking(setting.value);
          break;
        case hq::SettingId::MAX_HEADER_LIST_SIZE:
          break;
        case hq::SettingId::NUM_PLACEHOLDERS:
          // TODO: priorities not implemented yet
          break;
      }
    }
  }

  auto connCtrlStream =
      session_.findControlStream(UnidirectionalStreamType::CONTROL);
  auto g = folly::makeGuard(connCtrlStream->setActiveCodec(__func__));
  DCHECK(connCtrlStream);
  auto generated = connCtrlStream->codecFilterChain->generateSettings(
      connCtrlStream->writeBuf_);
  session_.scheduleWrite();
  return generated;
}

void HQSession::notifyPendingShutdown() {
  VLOG(4) << __func__ << " sess=" << *this;
  drainImpl();
}

void HQSession::closeWhenIdle() {
  VLOG(4) << __func__ << " sess=" << *this;
  drainImpl();
  if (version_ == HQVersion::H1Q_FB_V1) {
    drainState_ = DrainState::DONE;
  }
  checkForShutdown();
}

void HQSession::dropConnection() {
  dropConnectionWithError(
      std::make_pair(HTTP3::ErrorCode::HTTP_NO_ERROR, "Stopping"),
      kErrorDropped);
}

void HQSession::dropConnectionWithError(
    std::pair<quic::QuicErrorCode, std::string> errorCode,
    ProxygenError proxygenError) {
  VLOG(4) << __func__ << " sess=" << *this;
  HQSession::DestructorGuard dg(this);
  // dropping_ is used to guard against dropConnection->onError->dropConnection
  // re-entrancy. instead drainState_ = DONE means the connection can only be
  // deleted naturally in checkForShutdown.
  // We can get here with drainState_ == DONE, if somthing is holding a
  // DestructorGuardon the session when it gets dropped.
  if (dropping_ || drainState_ == DrainState::DONE) {
    VLOG(5) << "Already dropping sess=" << *this;
    return;
  }
  dropping_ = true;
  if (!streams_.empty()) {
    // should deliver errors to all open streams, they will all detach-
    sock_->close(std::move(errorCode));
    sock_.reset();
    setCloseReason(ConnectionCloseReason::SHUTDOWN);
    // If the txn had no registered cbs, there could be streams left
    // But we are not supposed to unregister the read callback, so this really
    // shouldn't happen
    invokeOnAllStreams([proxygenError](HQStreamTransport* stream) {
      stream->errorOnTransaction(proxygenError, "Dropped connection");
    });
  } else {
    // Can only be here if this wasn't fully drained. Cases like
    //  notify + drop  (PENDING)
    //  notify + CLOSE_SENT (in last request) + reset (no response) + drop
    //  CLOSE_RECEIVED (in last response) + drop
    // In any of these cases, it's ok to just close the socket
    // this should be closeNow()
    sock_->close(folly::none);
    sock_.reset();
  }
  drainState_ = DrainState::DONE;
  cancelLoopCallback();
  checkForShutdown();
  CHECK(streams_.empty());
}

void HQSession::checkForShutdown() {
  // For HQ upstream connections with a control stream, if the client wants to
  // go away, it can just stop creating new connections and set drainining
  // state to DONE, so that it will just shut down the socket when all the
  // request streams are done. In the process it will still be able to receive
  // and process GOAWAYs from the server
  // NOTE: this cannot be moved in VersionUtils, since we need to be able to
  // shutdown even before versionUtils is set in onTransportReady
  if (version_ != HQVersion::H1Q_FB_V1 &&
      direction_ == TransportDirection::UPSTREAM &&
      drainState_ == DrainState::PENDING) {
    drainState_ = DrainState::DONE;
  }

  // This is somewhat inefficient, checking every stream for possible detach
  // when we know explicitly earlier which ones are ready.  This is here to
  // minimize issues with iterator invalidation.
  for (auto it = streams_.begin(); it != streams_.end();) {
    auto& hqStream = it->second;
    ++it;
    hqStream.checkForDetach();
  }
  if (drainState_ == DrainState::DONE && streams_.empty() &&
      !isLoopCallbackScheduled()) {
    if (sock_) {
      sock_->close(folly::none);
      sock_.reset();
    }
    destroy();
  }
}

void HQSession::errorOnTransactionId(quic::StreamId id, HTTPException ex) {
  auto stream = findStream(id);
  if (stream) {
    stream->errorOnTransaction(std::move(ex));
  }
}

void HQSession::HQStreamTransportBase::errorOnTransaction(
    ProxygenError err, const std::string& errorMsg) {
  std::string extraErrorMsg;
  if (!errorMsg.empty()) {
    extraErrorMsg = folly::to<std::string>(". ", errorMsg);
  }

  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                   folly::to<std::string>(getErrorString(err),
                                          " on transaction id: ",
                                          getStreamId(),
                                          extraErrorMsg));
  ex.setProxygenError(err);
  errorOnTransaction(std::move(ex));
}

void HQSession::HQStreamTransportBase::errorOnTransaction(HTTPException ex) {
  if (!detached_) {
    txn_.onError(std::move(ex));
  }
  if (ex.isIngressException()) {
    abortIngress();
  }
  if (ex.isEgressException()) {
    abortEgress(true);
  }
}

HQSession::HQStreamTransport* FOLLY_NULLABLE
HQSession::findNonDetachedStream(quic::StreamId streamId) {
  auto it = streams_.find(streamId);
  if (it == streams_.end() || it->second.detached_) {
    return nullptr;
  } else {
    return &it->second;
  }
}

HQSession::HQStreamTransport* FOLLY_NULLABLE
HQSession::findStream(quic::StreamId streamId) {
  auto it = streams_.find(streamId);
  if (it == streams_.end()) {
    return nullptr;
  } else {
    return &it->second;
  }
}

HQSession::HQControlStream* FOLLY_NULLABLE
HQSession::findControlStream(UnidirectionalStreamType streamType) {
  auto it = controlStreams_.find(streamType);
  if (it == controlStreams_.end()) {
    return nullptr;
  } else {
    return &it->second;
  }
}

HQSession::HQControlStream* FOLLY_NULLABLE
HQSession::findControlStream(quic::StreamId streamId) {
  auto it = std::find_if(
      controlStreams_.begin(),
      controlStreams_.end(),
      [&](const std::pair<const UnidirectionalStreamType, HQControlStream>&
              entry) { return entry.second.isUsing(streamId); });
  if (it == controlStreams_.end()) {
    return nullptr;
  } else {
    return &it->second;
  }
}

void HQSession::runLoopCallback() noexcept {
  // We schedule this callback to run at the end of an event
  // loop iteration if either of two conditions has happened:
  //   * The session has generated some egress data (see scheduleWrite())
  //   * Reads have become unpaused (see resumeReads())

  inLoopCallback_ = true;
  HQSession::DestructorGuard dg(this);
  auto scopeg = folly::makeGuard([this] {
    // This ScopeGuard needs to be under the above DestructorGuard
    updatePendingWrites();
    checkForShutdown();
    inLoopCallback_ = false;
  });
  readsPerLoop_ = 0;

  // First process the read data
  //   - and maybe resume reads on the stream
  processReadData();

  versionUtils_->readDataProcessed();

  // Then handle the writes
  // Write all the control streams first
  maxToSend_ -= writeControlStreams(maxToSend_);
  // Then write the request streams
  if (!txnEgressQueue_.empty() && maxToSend_ > 0) {
    // TODO: we could send FIN only?
    writeRequestStreams(maxToSend_);
  }
  // Zero out maxToSend_ here.  We won't egress anything else until the next
  // onWriteReady call
  maxToSend_ = 0;

  if (!txnEgressQueue_.empty()) {
    scheduleWrite();
  }

  // Maybe schedule the next loop callback
  VLOG(4) << "sess=" << *this << " maybe schedule the next loop callback. "
          << " pending writes: " << !txnEgressQueue_.empty()
          << " pending processing reads: " << pendingProcessReadSet_.size();
  if (!pendingProcessReadSet_.empty()) {
    scheduleLoopCallback(false);
  }
  // checkForShutdown is now in ScopeGuard
}

void HQSession::HQVersionUtils::readDataProcessed() {
  auto ici = qpackCodec_.encodeInsertCountInc();
  if (ici) {
    auto QPACKDecoderStream =
        session_.findControlStream(UnidirectionalStreamType::QPACK_DECODER);
    DCHECK(QPACKDecoderStream);
    QPACKDecoderStream->writeBuf_.append(std::move(ici));
    // don't need to explicitly schedule write because this is called in the
    // loop before control streams are written
  }
}

void HQSession::scheduleWrite() {
  // always call for the whole connection and iterate trough all the streams
  // in onWriteReady
  if (scheduledWrite_) {
    return;
  }

  scheduledWrite_ = true;
  sock_->notifyPendingWriteOnConnection(this);
}

void HQSession::scheduleLoopCallback(bool thisIteration) {
  if (!isLoopCallbackScheduled()) {
    auto evb = getEventBase();
    if (evb) {
      evb->runInLoop(this, thisIteration);
    }
  }
}

void HQSession::resumeReads(quic::StreamId streamId) {
  VLOG(4) << __func__ << " sess=" << *this << ": resuming reads";
  sock_->resumeRead(streamId);
  scheduleLoopCallback(true);
  // TODO: ideally we should cancel the managed timeout when all the streams are
  // paused and then restart it when the timeouts are unpaused
}

void HQSession::pauseReads(quic::StreamId streamId) {
  VLOG(4) << __func__ << " sess=" << *this << ": pausing reads";
  sock_->pauseRead(streamId);
}

void HQSession::readAvailable(quic::StreamId id) noexcept {
  VLOG(4) << __func__ << " sess=" << *this
          << ": readAvailable on streamID=" << id;
  if (readsPerLoop_ >= kMaxReadsPerLoop) {
    VLOG(2) << __func__ << " sess=" << *this
            << ":skipping read for streamID=" << id
            << " maximum reads per loop reached";
    return;
  }
  readsPerLoop_++;
  readRequestStream(id);

  scheduleLoopCallback(true);
}

void HQSession::readError(
    quic::StreamId id,
    std::pair<quic::QuicErrorCode, folly::Optional<folly::StringPiece>>
        error) noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": readError streamID=" << id
          << " error: " << error;

  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                   folly::to<std::string>("Got error=", quic::toString(error)));

  // clang-format off
  folly::variant_match(
      error.first,
      [this, id, &ex](quic::ApplicationErrorCode ec) {
        auto errorCode = static_cast<HTTP3::ErrorCode>(ec);
        VLOG(3) << "readError: QUIC Application Error: " << toString(errorCode)
                << " streamID=" << id << " sess=" << *this;
        auto stream = findNonDetachedStream(id);
        if (stream) {
          stream->onResetStream(errorCode, std::move(ex));
        } else {
          // When a stream is erased, it's callback is cancelled, so it really
          // should be here
          VLOG(3) << "readError: received application error="
                  << toString(errorCode)
                  << " for detached streamID=" << id
                  << " sess=" << *this;
        }
      },
      [this, id, &ex](quic::LocalErrorCode errorCode) {
        VLOG(3) << "readError: QUIC Local Error: " << errorCode
                << " streamID=" << id << " sess=" << *this;
        if (errorCode == quic::LocalErrorCode::CONNECT_FAILED) {
          ex.setProxygenError(kErrorConnect);
        } else {
          ex.setProxygenError(kErrorShutdown);
        }
        errorOnTransactionId(id, std::move(ex));
      },
      [this, id, &ex](quic::TransportErrorCode errorCode) {
        VLOG(3) << "readError: QUIC Transport Error: " << errorCode
                << " streamID=" << id << " sess=" << *this;
        ex.setProxygenError(kErrorConnectionReset);
        // TODO: set Quic error when quic is OSS
        ex.setErrno(uint32_t(errorCode));
        errorOnTransactionId(id, std::move(ex));
      });
  // clang-format on
}

void HQSession::timeoutExpired() noexcept {
  VLOG(3) << "ManagedConnection timeoutExpired " << *this;
  if (!streams_.empty()) {
    VLOG(3) << "ignoring session timeout " << *this;
    resetTimeout();
    return;
  }
  VLOG(3) << "Timeout with nothing pending " << *this;
  setCloseReason(ConnectionCloseReason::TIMEOUT);
  closeWhenIdle();
}

HQSession::HQControlStream* FOLLY_NULLABLE
HQSession::tryCreateIngressControlStream(quic::StreamId id, uint64_t preface) {
  auto res = versionUtils_->parseStreamPreface(preface);
  if (!res) {
    LOG(ERROR) << "Got unidirectional stream with unknown preface "
               << static_cast<uint64_t>(preface) << " streamID=" << id
               << " sess=" << *this;
    return nullptr;
  }

  auto ctrlStream = createIngressControlStream(id, *res);
  if (!ctrlStream) {
    return nullptr;
  }

  sock_->setControlStream(id);
  return ctrlStream;
}

folly::Optional<UnidirectionalStreamType>
HQSession::H1QFBV2VersionUtils::parseStreamPreface(uint64_t preface) {
  auto prefaceEnum = static_cast<UnidirectionalStreamType>(preface);
  switch (prefaceEnum) {
    case UnidirectionalStreamType::H1Q_CONTROL:
      return prefaceEnum;
    default:
      break;
  }
  return folly::none;
}

folly::Optional<UnidirectionalStreamType>
HQSession::HQVersionUtils::parseStreamPreface(uint64_t preface) {
  auto prefaceEnum = static_cast<UnidirectionalStreamType>(preface);
  switch (prefaceEnum) {
    case UnidirectionalStreamType::CONTROL:
    case UnidirectionalStreamType::QPACK_ENCODER:
    case UnidirectionalStreamType::QPACK_DECODER:
      return prefaceEnum;
    default:
      break;
  }
  return folly::none;
}

void HQSession::readControlStream(HQControlStream* ctrlStream) {
  DCHECK(ctrlStream);
  auto readRes = sock_->read(ctrlStream->getIngressStreamId(), 0);
  if (readRes.hasError()) {
    LOG(ERROR) << "Got synchronous read error=" << readRes.error();
    readError(ctrlStream->getIngressStreamId(),
              {readRes.error(), folly::StringPiece("sync read error")});
    return;
  }
  resetTimeout();
  quic::Buf data = std::move(readRes.value().first);
  auto readSize = data ? data->computeChainDataLength() : 0;
  ctrlStream->readBuf_.append(std::move(data));
  ctrlStream->readEOF_ = readRes.value().second;

  if (infoCallback_) {
    infoCallback_->onRead(*this, readSize);
  }
  // GOAWAY may trigger session destroy, need a guard for that
  DestructorGuard dg(this);
  ctrlStream->processReadData();
}

void HQSession::processControlStreamPreface(
    quic::StreamId id,
    const folly::Range<quic::QuicSocket::PeekIterator>& peekData) noexcept {
  if (peekData.empty()) {
    LOG(ERROR) << __func__ << " invoked with no data";
    return;
  }
  // if not at offset 0, ignore
  if (peekData.front().offset != 0) {
    return;
  }
  // Look for a stream preface in the first read buffer
  folly::io::Cursor cursor(peekData.front().data.front());
  auto preface = quic::decodeQuicInteger(cursor);
  if (!preface) {
    return;
  }

  auto consumeRes = sock_->consume(id, preface->second);
  CHECK(!consumeRes.hasError());
  if (infoCallback_) {
    infoCallback_->onRead(*this, preface->second);
  }

  auto ctrlStream = tryCreateIngressControlStream(id, preface->first);
  if (!ctrlStream) {
    // Do not read data for unknown unidirectional stream types.
    // Send STOP_SENDING and rely on the peer sending a RESET to clear the
    // stream in the transport.
    sock_->stopSending(id, HTTP3::ErrorCode::HTTP_UNKNOWN_STREAM_TYPE);
    return;
  }

  // After reading the preface we can switch to the regular readCallback
  sock_->setPeekCallback(id, nullptr);
  sock_->setReadCallback(id, &unidirectionalReadCb_);
}

void HQSession::onDataAvailable(
    quic::StreamId id,
    const folly::Range<quic::QuicSocket::PeekIterator>& peekData) noexcept {
  if (sock_->isBidirectionalStream(id)) {
    auto hqStream = findNonDetachedStream(id);
    if (!hqStream) {
      if (streams_.find(id) != streams_.end()) {
        LOG(ERROR) << __func__ << " event received for detached stream " << id;
      }
      return;
    }
    hqStream->processPeekData(peekData);
  } else {
    processControlStreamPreface(id, peekData);
  }
}

void HQSession::unidirectionalReadAvailable(quic::StreamId id) {
  VLOG(4) << __func__ << " sess=" << *this << ": streamID=" << id;
  auto ctrlStream = findControlStream(id);
  if (!ctrlStream) {
    LOG(ERROR) << "Got readAvailable on unknown stream id=" << id
               << " sess=" << *this;
    return;
  }

  readControlStream(ctrlStream);
}

void HQSession::unidirectionalReadError(
    quic::StreamId id,
    std::pair<quic::QuicErrorCode, folly::Optional<folly::StringPiece>> error) {
  VLOG(4) << __func__ << " sess=" << *this << ": readError streamID=" << id
          << " error: " << error;

  auto ctrlStream = findControlStream(id);

  if (!ctrlStream) {
    bool shouldLog =
        folly::variant_match(error.first,
                             [&](quic::LocalErrorCode err) {
                               return err != quic::LocalErrorCode::NO_ERROR;
                             },
                             [](auto&) { return true; });
    LOG_IF(ERROR, shouldLog)
        << __func__ << " received read error=" << error
        << " for unknown control streamID=" << id << " sess=" << *this;
    return;
  }

  handleSessionError(ctrlStream, StreamDirection::INGRESS,
                     quicControlStreamError(error.first),
                     toProxygenError(error.first));
}

void HQSession::readRequestStream(quic::StreamId id) noexcept {
  auto hqStream = findNonDetachedStream(id);
  if (!hqStream) {
    // can we even get readAvailable after a stream is marked for detach ?
    DCHECK(streams_.find(id) != streams_.end());
    return;
  }
  // Read as much as you possibly can!
  auto readRes = sock_->read(id, 0);

  if (readRes.hasError()) {
    LOG(ERROR) << "Got synchronous read error=" << readRes.error();
    readError(id, {readRes.error(), folly::StringPiece("sync read error")});
    return;
  }
  resetTimeout();
  quic::Buf data = std::move(readRes.value().first);
  auto readSize = data ? data->computeChainDataLength() : 0;
  hqStream->readEOF_ = readRes.value().second;
  VLOG(3) << "Got streamID=" << hqStream->getStreamId() << " len=" << readSize
          << " eof=" << uint32_t(hqStream->readEOF_) << " sess=" << *this;
  if (hqStream->readEOF_) {
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hqStream->createdTime);
    QUIC_TRACE_SOCK(stream_event,
                    sock_,
                    "on_eom",
                    hqStream->getStreamId(),
                    folly::to<uint64_t>(timeDiff.count()));
  }
  // Just buffer the data and postpone processing in the loop callback
  hqStream->readBuf_.append(std::move(data));

  if (infoCallback_) {
    infoCallback_->onRead(*this, readSize);
  }

  pendingProcessReadSet_.insert(id);
}

void HQSession::processReadData() {
  for (auto it = pendingProcessReadSet_.begin();
       it != pendingProcessReadSet_.end();) {
    auto g = folly::makeGuard([&]() {
      // the codec may not have processed all the data, but we won't ask again
      // until we get more
      // TODO: set a timeout?
      it = pendingProcessReadSet_.erase(it);
    });
    auto hqIt = streams_.find(*it);
    if (hqIt == streams_.end()) {
      // ingress on a transaction may cause other transaction to get deleted
      continue;
    }
    auto& hqStream = hqIt->second;
    if (hqStream.detached_) {
      VLOG(4) << __func__ << " killing pending read data for detached txn="
              << hqStream.txn_;
      hqStream.readBuf_.move();
      hqStream.readEOF_ = false;
      continue;
    }
    // Feed it to the codec
    auto blocked = hqStream.processReadData();
    if (!blocked) {
      if (hqStream.readEOF_) {
        hqStream.onIngressEOF();
      }
    }
  }
}

void HQSession::H1QFBV1VersionUtils::headersComplete(HTTPMessage* msg) {
  // for h1q-fb-v1 start draining on receipt of a Connection:: close header
  if (session_.drainState_ == DrainState::DONE) {
    return;
  }
  if (msg->checkForHeaderToken(HTTP_HEADER_CONNECTION, "close", false)) {
    if (session_.drainState_ == DrainState::CLOSE_SENT) {
      session_.drainState_ = DrainState::DONE;
    } else {
      if (session_.drainState_ == DrainState::NONE) {
        session_.drainImpl();
      }
      session_.drainState_ = DrainState::CLOSE_RECEIVED;
    }
  }
}

void HQSession::HQVersionUtils::headersComplete(HTTPMessage* /*msg*/) {
  auto QPACKDecoderStream =
      session_.findControlStream(UnidirectionalStreamType::QPACK_DECODER);

  if (QPACKDecoderStream && !QPACKDecoderStream->writeBuf_.empty()) {
    session_.scheduleWrite();
  }
}

void HQSession::H1QFBV1VersionUtils::checkSendingGoaway(
    const HTTPMessage& msg) {
  if (session_.drainState_ == DrainState::NONE && !msg.wantsKeepalive()) {
    // Initiate the drain if the message explicitly requires no keepalive
    // NOTE: this will set the state to PENDING
    session_.notifyPendingShutdown();
  }

  if (session_.drainState_ == DrainState::CLOSE_RECEIVED) {
    session_.drainState_ = DONE;
  } else if (session_.drainState_ == DrainState::PENDING) {
    session_.drainState_ = DrainState::CLOSE_SENT;
  }
}

void HQSession::onSettings(const SettingsList& settings) {
  CHECK(versionUtils_);
  versionUtils_->onSettings(settings);
  receivedSettings_ = true;
}

void HQSession::HQVersionUtils::applySettings(const SettingsList& settings) {
  DestructorGuard g(&session_);
  VLOG(3) << "Got SETTINGS sess=" << session_;

  uint32_t tableSize = kDefaultIngressHeaderTableSize;
  uint32_t blocked = kDefaultIngressQpackBlockedStream;
  uint32_t numPlaceholders = kDefaultIngressNumPlaceHolders;
  for (auto& setting : settings) {
    auto id = httpToHqSettingsId(setting.id);
    if (id) {
      switch (*id) {
        case hq::SettingId::HEADER_TABLE_SIZE:
          tableSize = setting.value;
          break;
        case hq::SettingId::QPACK_BLOCKED_STREAMS:
          blocked = setting.value;
          break;
        case hq::SettingId::MAX_HEADER_LIST_SIZE:
          // this setting is stored in ingressSettings_ and enforced in the
          // StreamCodec
          break;
        case hq::SettingId::NUM_PLACEHOLDERS:
          numPlaceholders = setting.value;
          break;
      }
    }
  }
  qpackCodec_.setEncoderHeaderTableSize(tableSize);
  qpackCodec_.setMaxVulnerable(blocked);
  // TODO: set the num placeholder value
  VLOG(3) << "Applied SETTINGS sess=" << session_ << " size=" << tableSize
          << " blocked=" << blocked;
}

void HQSession::HQVersionUtils::onSettings(const SettingsList& settings) {
  applySettings(settings);
  if (session_.infoCallback_) {
    session_.infoCallback_->onSettings(session_, settings);
  }
}

void HQSession::onGoaway(uint64_t lastGoodStreamID,
                         ErrorCode code,
                         std::unique_ptr<folly::IOBuf> /* debugData */) {
  // NOTE: This function needs to be idempotent. i.e. be a no-op if invoked
  // twice with the same lastGoodStreamID
  DCHECK_EQ(direction_, TransportDirection::UPSTREAM);
  DCHECK(version_ != HQVersion::H1Q_FB_V1);
  VLOG(3) << "Got GOAWAY maxStreamID=" << lastGoodStreamID << " sess=" << *this;
  maxAllowedStreamId_ = std::min(maxAllowedStreamId_, lastGoodStreamID);
  setCloseReason(ConnectionCloseReason::GOAWAY);
  // drains existing streams and prevents new streams to be created
  drainImpl();

  invokeOnNonDetachedStreams([this, code](HQStreamTransport* stream) {
    // Invoke onGoaway on all transactions
    stream->txn_.onGoaway(code);
    // Abort transactions which have been initiated locally but not created
    // successfully at the remote end
    // TODO: change this in (stream->getStreamId() >= maxAllowedStreamId_)
    // (see https://github.com/quicwg/base-drafts/issues/1717)
    if (stream->getStreamId() > maxAllowedStreamId_) {
      stream->errorOnTransaction(kErrorStreamUnacknowledged, "");
    }
  });

  if (drainState_ == DrainState::NONE || drainState_ == DrainState::PENDING) {
    drainState_ = DrainState::FIRST_GOAWAY;
  } else if (drainState_ == DrainState::FIRST_GOAWAY) {
    drainState_ = DrainState::DONE;
  }

  checkForShutdown();
}

void HQSession::pauseTransactions() {
  for (auto& it : streams_) {
    it.second.txn_.pauseEgress();
  }
}

void HQSession::notifyEgressBodyBuffered(int64_t bytes) {
  if (HTTPSessionBase::notifyEgressBodyBuffered(bytes, true) &&
      !inLoopCallback_ && !isLoopCallbackScheduled() && sock_) {
    sock_->getEventBase()->runInLoop(this);
  }
}

void HQSession::onFlowControlUpdate(quic::StreamId id) noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": streamID=" << id;

  auto flowControl = sock_->getStreamFlowControl(id);
  if (flowControl.hasError()) {
    LOG(ERROR) << "Got error=" << flowControl.error() << " streamID=" << id;
    return;
  }

  auto ctrlStream = findControlStream(id);
  if (ctrlStream && flowControl->sendWindowAvailable > 0) {
    QUIC_TRACE_SOCK(stream_event,
                    sock_,
                    "on_flow_control",
                    id,
                    flowControl->sendWindowAvailable);
    scheduleWrite();
    return;
  }

  auto streamIt = streams_.find(id);
  if (streamIt == streams_.end()) {
    LOG(ERROR) << "Got flow control update for unknown streamID=" << id
               << " sess=" << this;
    return;
  }

  auto stream = &streamIt->second;

  auto& txn = stream->txn_;
  // Check if this stream has flow control, or has only EOM pending
  if (flowControl->sendWindowAvailable > 0 ||
      (!stream->hasPendingBody() && stream->hasPendingEOM())) {
    // TODO: are we intentionally piggyback the time value for flow control
    // window here?
    QUIC_TRACE_SOCK(stream_event,
                    sock_,
                    "on_flow_control",
                    stream->getStreamId(),
                    flowControl->sendWindowAvailable);
    if (stream->hasPendingEgress()) {
      txnEgressQueue_.signalPendingEgress(stream->queueHandle_.getHandle());
    }
    if (!stream->detached_ && txn.isEgressPaused()) {
      // txn might be paused
      txn.resumeEgress();
    }
    scheduleWrite();
  }
}

void HQSession::onConnectionWriteReady(uint64_t maxToSend) noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": maxToSend=" << maxToSend;
  scheduledWrite_ = false;
  maxToSend_ = maxToSend;

  scheduleLoopCallback(true);
}

void HQSession::onConnectionWriteError(
    std::pair<quic::QuicErrorCode, folly::Optional<folly::StringPiece>>
        error) noexcept {
  scheduledWrite_ = false;
  VLOG(4) << __func__ << " sess=" << *this << ": writeError error=" << error;
  // Leave this as a no-op.  We will most likely get onConnectionError soon
}

uint64_t HQSession::writeControlStreams(uint64_t maxEgress) {
  uint64_t maxEgressOrig = maxEgress;
  // NOTE: process the control streams in the order they are stored
  // this could potentially lead to stream starvation
  for (auto& it : controlStreams_) {
    if (it.second.writeBuf_.empty()) {
      continue;
    }
    auto sent = controlStreamWriteImpl(&it.second, maxEgress);
    DCHECK_LE(sent, maxEgress);
    maxEgress -= sent;
    if (maxEgress == 0) {
      break;
    }
  }
  return maxEgressOrig - maxEgress;
}

uint64_t HQSession::controlStreamWriteImpl(HQControlStream* ctrlStream,
                                           uint64_t maxEgress) {
  auto egressStreamId = ctrlStream->getEgressStreamId();
  auto flowControl = sock_->getStreamFlowControl(egressStreamId);
  if (flowControl.hasError()) {
    LOG(ERROR)
        << "Got error=" << flowControl.error() << " streamID=" << egressStreamId
        << " bufLen=" << static_cast<int>(ctrlStream->writeBuf_.chainLength())
        << " readEOF=" << ctrlStream->readEOF_;
    handleSessionError(
        ctrlStream, StreamDirection::EGRESS,
        quicControlStreamError(flowControl.error()),
        toProxygenError(flowControl.error()));
    return 0;
  }

  auto streamSendWindow = flowControl->sendWindowAvailable;
  size_t canSend = std::min(streamSendWindow, maxEgress);
  auto sendLen = std::min(canSend, ctrlStream->writeBuf_.chainLength());
  auto tryWriteBuf = ctrlStream->writeBuf_.splitAtMost(canSend);

  CHECK_GE(sendLen, 0);
  VLOG(4) << __func__ << " before write sess=" << *this
          << ": streamID=" << egressStreamId << " maxEgress=" << maxEgress
          << " sendWindow=" << streamSendWindow
          << " tryToSend=" << tryWriteBuf->computeChainDataLength();

  auto writeRes = writeBase(egressStreamId,
                            &ctrlStream->writeBuf_,
                            std::move(tryWriteBuf),
                            sendLen,
                            false,
                            nullptr);

  if (writeRes.hasError()) {
    // Going to call this a write error no matter what the underlying reason was
    handleSessionError(ctrlStream, StreamDirection::EGRESS,
                       quicControlStreamError(writeRes.error()),
                       kErrorWrite);
    return 0;
  }

  size_t sent = *writeRes;
  VLOG(4)
      << __func__ << " after write sess=" << *this
      << ": streamID=" << ctrlStream->getEgressStreamId() << " sent=" << sent
      << " buflen=" << static_cast<int>(ctrlStream->writeBuf_.chainLength());
  if (infoCallback_) {
    infoCallback_->onWrite(*this, sent);
  }

  CHECK_GE(maxEgress, sent);
  return sent;
}

void HQSession::handleSessionError(HQStreamBase* stream,
                                   StreamDirection streamDir,
                                   quic::QuicErrorCode err,
                                   ProxygenError proxygenError) {
  // This is most likely a control stream
  std::string appErrorMsg;
  HTTP3::ErrorCode appError = HTTP3::ErrorCode::HTTP_NO_ERROR;
  auto ctrlStream = dynamic_cast<HQControlStream*>(stream);
  if (ctrlStream) {
    auto id = (streamDir == StreamDirection::EGRESS
                   ? ctrlStream->getEgressStreamId()
                   : ctrlStream->getIngressStreamId());
    // TODO: This happens for each control stream during shutdown, and that is
    // too much for a LOG(ERROR)
    VLOG(3) << "Got error on control stream error=" << err << " streamID=" << id
            << " Dropping connection. sess=" << *this;
    appErrorMsg = "HTTP error on control stream";
  } else {
    auto requestStream = dynamic_cast<HQStreamTransport*>(stream);
    CHECK(requestStream);
    auto id = requestStream->getEgressStreamId();
    LOG(ERROR) << "Got error on request stream error=" << err
               << " streamID=" << id << " Dropping connection. sess=" << *this;
    appErrorMsg = "HTTP error on request stream";
    // for request streams this function must be called with an ApplicationError
    folly::variant_match(err,
                         [&](quic::ApplicationErrorCode error) {},
                         [](auto&) { DCHECK(false); });
  }
  // errors on a control stream means we must drop the entire connection,
  // but there are some errors that we expect during shutdown
  bool shouldDrop = folly::variant_match(
      err,
      [&](quic::ApplicationErrorCode error) {
        // An ApplicationErrorCode is expected when
        //  1. The peer resets a control stream
        //  2. A control codec detects a connection error on a control stream
        //  3. A stream codec detects a connection level error (eg: compression)
        // We always want to drop the connection in these cases.
        appError = static_cast<HTTP3::ErrorCode>(error);
        return true;
      },
      [](quic::LocalErrorCode errorCode) {
        // a LocalErrorCode::NO_ERROR is expected whenever the socket gets
        // closed without error
        return (errorCode != quic::LocalErrorCode::NO_ERROR);
      },
      [](quic::TransportErrorCode errorCode) {
        // a TransportErrorCode::NO_ERROR is expected whenever the socket gets
        // closed without error from the remote
        return (errorCode != quic::TransportErrorCode::NO_ERROR);
      },
      [](auto&) { return true; });

  if (!shouldDrop) {
    return;
  }

  if (ctrlStream && appError == HTTP3::ErrorCode::HTTP_NO_ERROR) {
    // If we got a local or transport error reading or writing on a
    // control stream, send CLOSED_CRITICAL_STREAM.
    appError = HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM;
  }
  // we cannot just simply drop the connection here, since in case of a
  // close received from the remote, we may have other readError callbacks on
  // other streams after this one. So run in the next loop iteration
  auto evb = getEventBase();
  if (evb) {
    HQSession::DestructorGuard dg(this);
    evb->runInLoop(
        [this, appError, appErrorMsg, proxygenError, sessionDg = dg]() {
          dropConnectionWithError(std::make_pair(appError, appErrorMsg),
                                  proxygenError);
        },
        false);
  }
}

void HQSession::writeRequestStreams(uint64_t maxEgress) noexcept {
  // requestStreamWriteImpl may call txn->onWriteReady
  txnEgressQueue_.nextEgress(nextEgressResults_);
  for (auto it = nextEgressResults_.begin(); it != nextEgressResults_.end();
       ++it) {
    auto& ratio = it->second;
    auto hqStream = static_cast<HQStreamTransport*>(&it->first->getTransport());

    // TODO: scale maxToSend by ratio?
    auto sent = requestStreamWriteImpl(hqStream, maxEgress, ratio);
    DCHECK_LE(sent, maxEgress);
    maxEgress -= sent;

    if (maxEgress == 0 && std::next(it) != nextEgressResults_.end()) {
      VLOG(2) << __func__ << " sess=" << *this
              << "got more to send than the transport could take";
      break;
    }
  }
  nextEgressResults_.clear();
}

void HQSession::handleWriteError(HQStreamTransport* hqStream,
                                 quic::QuicErrorCode err) {
  // We call this INGRESS_AND_EGRESS so it fully terminates the
  // HTTPTransaction state machine.
  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                   folly::to<std::string>("Got error=", quic::toString(err)));
  // TODO: set Quic error when quic is OSS
  folly::variant_match(err,
                       [&ex](quic::ApplicationErrorCode error) {
                         // If we have an application error code, it must have
                         // come from the peer (most likely STOP_SENDING). This
                         // is logically a stream abort, not a write error
                         ex.setCodecStatusCode(hqToHttpErrorCode(
                             static_cast<HTTP3::ErrorCode>(error)));
                         ex.setProxygenError(kErrorStreamAbort);
                         return true;
                       },
                       [&ex](quic::LocalErrorCode errorCode) {
                         ex.setErrno(uint32_t(errorCode));
                         ex.setProxygenError(kErrorWrite);
                         return true;
                       },
                       [](quic::TransportErrorCode errorCode) {
                         CHECK(false) << "Unexpected errorCode=" << errorCode;
                         return false;
                       });
  // Do I need a dguard here?
  abortStream(ex.getDirection(),
              hqStream->getStreamId(),
              HTTP3::ErrorCode::HTTP_REQUEST_CANCELLED);
  hqStream->errorOnTransaction(std::move(ex));
}

folly::Expected<size_t, quic::LocalErrorCode> HQSession::writeBase(
    quic::StreamId id,
    folly::IOBufQueue* writeBuf,
    std::unique_ptr<folly::IOBuf> data,
    size_t tryToSend,
    bool sendEof,
    quic::QuicSocket::DeliveryCallback* deliveryCallback) {

  auto writeRes = sock_->writeChain(id,
                                    std::move(data),
                                    sendEof,
                                    false, // cork
                                    deliveryCallback);
  if (writeRes.hasError()) {
    LOG(ERROR) << " Got error=" << writeRes.error() << " streamID=" << id;
    return folly::makeUnexpected(writeRes.error());
  }

  auto notWrittenBuf = std::move(writeRes.value());
  auto sent = tryToSend;
  if (notWrittenBuf && !notWrittenBuf->empty()) {
    // The transport gave back some data, prepend to the write buffer.
    // According to the QuicSocket API this should never happen if we are
    // enforcing the flow control limits
    VLOG(4) << "stream " << id << " got "
            << notWrittenBuf->computeChainDataLength()
            << " bytes back from the transport";
    sent -= notWrittenBuf->computeChainDataLength();
    VLOG(4) << __func__ << " sess=" << *this << ": streamID=" << id
            << " tryToSend: " << tryToSend << " actual bytes sent: " << sent;
    auto tmpBuf = writeBuf->move();
    writeBuf->append(std::move(notWrittenBuf));
    writeBuf->append(std::move(tmpBuf));
  }
  return sent;
}

size_t HQSession::handleWrite(HQStreamTransport* hqStream,
                              std::unique_ptr<folly::IOBuf> data,
                              size_t tryToSend,
                              bool sendEof) {
  quic::QuicSocket::DeliveryCallback* deliveryCallback =
      sendEof ? this : nullptr;

  auto writeRes = writeBase(hqStream->getEgressStreamId(),
                            &hqStream->writeBuf_,
                            std::move(data),
                            tryToSend,
                            sendEof,
                            deliveryCallback);
  if (writeRes.hasError()) {
    handleWriteError(hqStream, writeRes.error());
    return 0;
  }

  auto sent = *writeRes;
  if (sent == tryToSend && sendEof) {
    // This will hold the transaction open until onDeliveryAck or onCanceled
    DCHECK(deliveryCallback);
    hqStream->txn_.incrementPendingByteEvents();
    // NOTE: This may not be necessary long term, once we properly implement
    // detach or when we enforce flow control for headers and EOM
    hqStream->pendingEOM_ = false;
  }
  hqStream->bytesWritten_ += sent;
  // hqStream's byteEventTracker cannot be changed, so no need to pass
  // shared_ptr or use in while loop
  hqStream->byteEventTracker_.processByteEvents(nullptr,
                                                hqStream->bytesWritten_);
  return sent;
}

uint64_t HQSession::requestStreamWriteImpl(HQStreamTransport* hqStream,
                                           uint64_t maxEgress,
                                           double ratio) {
  CHECK(hqStream->queueHandle_.isStreamTransportEnqueued());
  HTTPTransaction::DestructorGuard dg(&hqStream->txn_);

  auto streamId = hqStream->getStreamId();
  auto flowControl = sock_->getStreamFlowControl(streamId);
  if (flowControl.hasError()) {
    LOG(ERROR)
        << "Got error=" << flowControl.error() << " streamID=" << streamId
        << " detached=" << hqStream->detached_
        << " readBufLen=" << static_cast<int>(hqStream->readBuf_.chainLength())
        << " writeBufLen="
        << static_cast<int>(hqStream->writeBuf_.chainLength())
        << " readEOF=" << hqStream->readEOF_
        << " ingressError_=" << static_cast<int>(hqStream->ingressError_)
        << " eomGate_=" << hqStream->eomGate_;
    handleWriteError(hqStream, flowControl.error());
    return 0;
  }

  auto streamSendWindow = flowControl->sendWindowAvailable;

  size_t canSend = std::min(streamSendWindow, maxEgress);

  // we may have already buffered more than the amount the transport can take,
  // or the txn may not have any more body bytes/EOM to add. In case, there is
  // no need to call txn->onWriteReady.
  if (hqStream->wantsOnWriteReady(canSend)) {
    // Populate the write buffer by telling the transaction how much
    // room is available for body data
    size_t maxBodySend = canSend - hqStream->writeBuf_.chainLength();
    VLOG(4) << __func__ << " asking txn for more bytes sess=" << *this
            << ": streamID=" << streamId << " canSend=" << canSend
            << " remain=" << hqStream->writeBuf_.chainLength()
            << " pendingEOM=" << hqStream->pendingEOM_
            << " maxBodySend=" << maxBodySend << " ratio=" << ratio;
    hqStream->txn_.onWriteReady(maxBodySend, ratio);
    // onWriteReady may not be able to detach any byte from the deferred egress
    // body bytes, in case it's getting rate limited.
    // In that case the txn will get removed from the egress queue from
    // onWriteReady
    if (hqStream->writeBuf_.empty() && !hqStream->pendingEOM_) {
      return 0;
    }
  }
  auto sendLen = std::min(canSend, hqStream->writeBuf_.chainLength());
  auto tryWriteBuf = hqStream->writeBuf_.splitAtMost(canSend);
  bool sendEof = (hqStream->pendingEOM_ && !hqStream->hasPendingBody());

  CHECK(sendLen > 0 || sendEof);
  VLOG(4) << __func__ << " before write sess=" << *this
          << ": streamID=" << streamId << " maxEgress=" << maxEgress
          << " sendWindow=" << streamSendWindow
          << " tryToSend=" << tryWriteBuf->computeChainDataLength()
          << " sendEof=" << sendEof;

  size_t sent = handleWrite(hqStream, std::move(tryWriteBuf), sendLen, sendEof);

  VLOG(4) << __func__ << " after write sess=" << *this
          << ": streamID=" << streamId << " sent=" << sent
          << " buflen=" << static_cast<int>(hqStream->writeBuf_.chainLength())
          << " hasPendingBody=" << hqStream->txn_.hasPendingBody()
          << " EOM=" << hqStream->pendingEOM_;
  if (infoCallback_) {
    infoCallback_->onWrite(*this, sent);
  }
  CHECK_GE(maxEgress, sent);

  bool flowControlBlocked = (sent == streamSendWindow && !sendEof);
  if (flowControlBlocked) {
    // TODO: this one doesn't create trouble, but it's certainly not logging the
    // extra params anyway.
    QUIC_TRACE_SOCK(stream_event,
                    sock_,
                    "stream_blocked",
                    streamId,
                    streamSendWindow,
                    canSend,
                    (int)hqStream->hasPendingEgress());
  }
  // sendAbort can clear the egress queue, so this stream may no longer be
  // enqueued
  if (hqStream->queueHandle_.isStreamTransportEnqueued() &&
      (!hqStream->hasPendingEgress() || flowControlBlocked)) {
    VLOG(4) << "clearPendingEgress for " << hqStream->txn_;
    txnEgressQueue_.clearPendingEgress(hqStream->queueHandle_.getHandle());
  }
  if (flowControlBlocked && !hqStream->txn_.isEgressComplete()) {
    VLOG(4) << __func__ << " txn flow control blocked, txn=" << hqStream->txn_;
    hqStream->txn_.pauseEgress();
  }
  return sent;
}

void HQSession::onDeliveryAck(quic::StreamId id,
                              uint64_t offset,
                              std::chrono::microseconds rtt) {
  VLOG(4) << __func__ << " sess=" << *this << ": streamID=" << id
          << " offset=" << offset;
  auto streamIt = streams_.find(id);
  DCHECK(streamIt != streams_.end());
  auto stream = &streamIt->second;
  stream->txn_.onEgressLastByteAck(
      std::chrono::duration_cast<std::chrono::milliseconds>(rtt));
  stream->txn_.decrementPendingByteEvents();
}

void HQSession::onCanceled(quic::StreamId id, uint64_t /*offset*/) {
  VLOG(4) << __func__ << " sess=" << *this << ": streamID=" << id;
  auto streamIt = streams_.find(id);
  if (streamIt != streams_.end()) {
    auto stream = &streamIt->second;
    stream->txn_.decrementPendingByteEvents();
  } else {
    LOG(DFATAL) << __func__ << " sess=" << *this << ": streamID=" << id
                << " onCanceled but txn missing, aborted without reset?";
  }
}

void HQSession::HQControlStream::onDeliveryAck(
    quic::StreamId id, uint64_t /*offset*/, std::chrono::microseconds /*rtt*/) {
  // We set the delivery callback for the control stream to keep track of the
  // GOAWAY being delivered to the remote endpoint. When that happens we can
  // send a second GOAWAY. sendGoaway is a no-op after the second time
  VLOG(3) << "GOAWAY received by remote endpoint on streamID=" << id
          << " sess=" << session_;
  session_.onGoawayAck();
}

void HQSession::HQControlStream::onCanceled(quic::StreamId id,
                                            uint64_t /*offset*/) {
  // This shouldn't really happen, but in case it does let's accelerate draining
  VLOG(3) << "GOAWAY delivery callback canceled on streamID=" << id
          << " sess=" << session_;
  session_.drainState_ = DrainState::DONE;
  // if we are shutting down, do so in the loop callback
  session_.scheduleLoopCallback(false);
}

void HQSession::onGoawayAck() {
  if (drainState_ == DrainState::FIRST_GOAWAY) {
    versionUtils_->sendGoaway();
  } else if (drainState_ == DrainState::SECOND_GOAWAY) {
    drainState_ = DrainState::DONE;
  }
  // if we are shutting down, do so in the loop callback
  scheduleLoopCallback(false);
}

HQSession::HQStreamTransport* FOLLY_NULLABLE
HQSession::createStreamTransport(quic::StreamId streamId) {
  VLOG(4) << __func__ << " sess=" << *this;
  if (!sock_->good() || streams_.count(streamId)) {
    // Refuse to add a transaction on a closing session or if a
    // transaction of that ID already exists.
    return nullptr;
  }

  if (streams_.empty()) {
    if (infoCallback_) {
      infoCallback_->onActivateConnection(*this);
    }
    if (getConnectionManager()) {
      getConnectionManager()->onActivated(*this);
    }
  }

  // The transport should never call createStreamTransport before
  // onTransportReady
  DCHECK(versionUtils_) << "The transport should never call " << __func__
                        << " before onTransportReady";
  std::unique_ptr<HTTPCodec> codec = versionUtils_->createCodec(streamId);
  auto matchPair = streams_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(streamId),
      std::forward_as_tuple(
          *this,
          direction_,
          streamId,
          getNumTxnServed(),
          std::move(codec),
          WheelTimerInstance(transactionsTimeout_, getEventBase()),
          nullptr //   HTTPSessionStats* sessionStats_
          ));
  incrementSeqNo();

  CHECK(matchPair.second) << "Emplacement failed, despite earlier "
                             "existence check.";

  if (versionUtils_ && drainState_ != DrainState::NONE) {
    versionUtils_->sendGoawayOnRequestStream(matchPair.first->second);
  }

  return &matchPair.first->second;
}

std::unique_ptr<HTTPCodec> HQSession::H1QFBV1VersionUtils::createCodec(
    quic::StreamId /*streamId*/) {
  return std::make_unique<HTTP1xCodec>(session_.direction_,
                                       session_.forceUpstream1_1_);
}

std::unique_ptr<HTTPCodec> HQSession::HQVersionUtils::createCodec(
    quic::StreamId streamId) {
  auto QPACKEncoderStream =
      session_.findControlStream(UnidirectionalStreamType::QPACK_ENCODER);
  DCHECK(QPACKEncoderStream);
  auto QPACKDecoderStream =
      session_.findControlStream(UnidirectionalStreamType::QPACK_DECODER);
  DCHECK(QPACKDecoderStream);
  return std::make_unique<hq::HQStreamCodec>(
      streamId,
      session_.direction_,
      qpackCodec_,
      QPACKEncoderStream->writeBuf_,
      QPACKDecoderStream->writeBuf_,
      [this, id = QPACKEncoderStream->getEgressStreamId()] {
        if (!session_.sock_) {
          return uint64_t(0);
        }
        auto res = session_.sock_->getStreamFlowControl(id);
        if (res.hasError()) {
          return uint64_t(0);
        }
        return res->sendWindowAvailable;
      },
      session_.egressSettings_,
      session_.ingressSettings_,
      false);
}

void HQSession::H1QFBV1VersionUtils::sendGoawayOnRequestStream(
    HQSession::HQStreamTransport& stream) {
  stream.generateGoaway();
}

HQSession::HQStreamTransportBase::HQStreamTransportBase(
    HQSession& session,
    TransportDirection direction,
    quic::StreamId id,
    uint32_t seqNo,
    std::unique_ptr<HTTPCodec> codec,
    const WheelTimerInstance& timeout,
    HTTPSessionStats* stats,
    http2::PriorityUpdate priority,
    folly::Optional<HTTPCodec::StreamID> parentTxnId)
    : HQStreamBase(session, session.codec_),
      HTTP2PriorityQueueBase(kSessionStreamId),
      txn_(direction,
           static_cast<HTTPCodec::StreamID>(id),
           seqNo,
           *this,
           *this,
           timeout.getWheelTimer(),
           timeout.getDefaultTimeout(),
           stats,
           false, // useFlowControl
           0,     // receiveInitialWindowSize
           0,     // sendInitialWindowSize,
           priority,
           parentTxnId),
      byteEventTracker_(nullptr) {
  VLOG(4) << __func__ << " txn=" << txn_;
  CHECK(session_.sock_)
      << "Socket is null drainState=" << (int)session_.drainState_
      << " streams=" << (int)session_.streams_.size();
  realCodec_ = std::move(codec);
  if (session_.version_ == HQVersion::HQ) {
    auto c = dynamic_cast<hq::HQStreamCodec*>(realCodec_.get());
    CHECK(c);
    c->setActivationHook([this] { return setActiveCodec("self"); });
  }
  auto g = folly::makeGuard(setActiveCodec(__func__));
  if (direction == TransportDirection::UPSTREAM) {
    codecStreamId_ = codecFilterChain->createStream();
  }
  if (session_.receiveStreamWindowSize_.hasValue()) {
    session_.sock_->setStreamFlowControlWindow(
        id, session_.receiveStreamWindowSize_.value());
  }
  codecFilterChain->setCallback(this);
  byteEventTracker_.setTTLBAStats(session_.sessionStats_);
  quicStreamProtocolInfo_ = std::make_shared<QuicStreamProtocolInfo>();
  eomGate_.then([this] { txn_.onIngressEOM(); });
}

HTTPTransaction* FOLLY_NULLABLE
HQSession::newTransaction(HTTPTransaction::Handler* handler) {
  VLOG(4) << __func__ << " sess=" << *this;
  if (drainState_ == DrainState::CLOSE_SENT ||
      drainState_ == DrainState::FIRST_GOAWAY ||
      drainState_ == DrainState::DONE) {
    VLOG(4) << __func__ << " newTransaction after drain: " << *this;
    return nullptr;
  }
  if (!sock_->good()) {
    VLOG(4) << __func__ << " newTransaction after sock went bad: " << this;
    return nullptr;
  }
  // TODO stream limit handling
  auto quicStreamId = sock_->createBidirectionalStream();
  if (!quicStreamId) {
    VLOG(4) << __func__ << " failed to create new stream: " << this;
    return nullptr;
  }

  auto hqStream = createStreamTransport(quicStreamId.value());
  if (hqStream) {
    // DestructorGuard dg(this);
    hqStream->txn_.setHandler(CHECK_NOTNULL(handler));
    setNewTransactionPauseState(&hqStream->txn_);
    sock_->setReadCallback(quicStreamId.value(), this);
    return &hqStream->txn_;
  }
  return nullptr;
}

void HQSession::startNow() {
  VLOG(4) << __func__ << " sess=" << *this;
  CHECK(!started_);
  CHECK(sock_);
  started_ = true;
  transportInfo_.secure = true;
  transportInfo_.validTcpinfo = true;
  transportStart_ = getCurrentTime();
  // TODO: invoke socket.start() here
  resetTimeout();
}

void HQSession::HQStreamTransportBase::checkForDetach() {
  if (detached_ && readBuf_.empty() && writeBuf_.empty() && !pendingEOM_ &&
      !queueHandle_.isStreamTransportEnqueued()) {
    session_.detachStreamTransport(this);
  }
}

bool HQSession::HQStreamTransportBase::getCurrentTransportInfo(
    wangle::TransportInfo* tinfo) {
  VLOG(4) << __func__ << " txn=" << txn_;
  CHECK(quicStreamProtocolInfo_.get());
  bool success = session_.getCurrentTransportInfo(tinfo);

  // Save connection-level protocol fields in the HQStreamTransport-level
  // protocol info.
  if (success) {
    auto connectionTransportInfo =
        dynamic_cast<QuicProtocolInfo*>(tinfo->protocolInfo.get());
    if (connectionTransportInfo) {
      // NOTE: slicing assignment; stream-level fields of
      // quicStreamProtocolInfo_ are not changed while the connection
      // level fields are overwritten.
      *quicStreamProtocolInfo_ = *connectionTransportInfo;
    }
  }

  // Update the HQStreamTransport-level protocol info with the
  // stream info from the the QUIC transport
  session_.getCurrentStreamTransportInfo(quicStreamProtocolInfo_.get(),
                                         getStreamId());

  // Set the transport info query result to the HQStreamTransport protocol
  // info
  tinfo->protocolInfo = quicStreamProtocolInfo_;

  return success;
}

void HQSession::detachStreamTransport(HQStreamTransportBase* hqStream) {
  auto streamId = hqStream->getStreamId();
  VLOG(4) << __func__ << " streamID=" << streamId;
  auto it = streams_.find(streamId);
  CHECK(it != streams_.end());
  if (sock_) {
    sock_->setReadCallback(streamId, nullptr);
  }
  streams_.erase(it);
  if (streams_.empty()) {
    if (infoCallback_) {
      infoCallback_->onDeactivateConnection(*this);
    }
    if (getConnectionManager()) {
      getConnectionManager()->onDeactivated(*this);
    }
    resetTimeout();
  } else {
    if (infoCallback_) {
      infoCallback_->onTransactionDetached(*this);
    }
  }
}

void HQSession::HQControlStream::processReadData() {
  auto g = folly::makeGuard(setActiveCodec(__func__));
  CHECK(ingressCodec_->isIngress());
  auto initialLength = readBuf_.chainLength();
  if (initialLength > 0) {
    auto ret = ingressCodec_->onUnidirectionalIngress(readBuf_.move());
    VLOG(4) << "streamID=" << getIngressStreamId() << " parsed bytes="
            << static_cast<int>(initialLength - readBuf_.chainLength())
            << " from readBuf remain=" << readBuf_.chainLength()
            << " eof=" << readEOF_;
    readBuf_.append(std::move(ret));
  }
  if (readEOF_ && readBuf_.chainLength() == 0) {
    ingressCodec_->onUnidirectionalIngressEOF();
  }
}

bool HQSession::HQStreamTransportBase::processReadData() {
  auto g = folly::makeGuard(setActiveCodec(__func__));
  const folly::IOBuf* currentReadBuf;
  if (eomGate_.get(EOMType::CODEC) && readBuf_.chainLength() > 0) {
    // why are we calling processReadData with no data?
    VLOG(3) << " Received data after HTTP EOM for txn=" << txn_
            << ", len=" << static_cast<int>(readBuf_.chainLength());
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                     "Unexpected data after request");
    errorOnTransaction(std::move(ex));
    return false;
  }
  while (!ingressError_ && (currentReadBuf = readBuf_.front()) != nullptr &&
         currentReadBuf->length() != 0) {
    size_t bytesParsed = codecFilterChain->onIngress(*currentReadBuf);
    VLOG(4) << "streamID=" << getStreamId()
            << " parsed bytes=" << static_cast<int>(bytesParsed)
            << " from readBuf remain=" << readBuf_.chainLength()
            << " eof=" << readEOF_;
    if (bytesParsed == 0) {
      break;
    }
    readBuf_.trimStart(bytesParsed);
  }
  if (ingressError_) {
    abortIngress();
  }
  return (readBuf_.chainLength() > 0);
}

void HQSession::HQStreamTransportBase::onHeadersComplete(
    HTTPCodec::StreamID streamID, std::unique_ptr<HTTPMessage> msg) {
  VLOG(4) << __func__ << " txn=" << txn_;
  msg->dumpMessage(3);
  // TODO: the codec will set this for non-H1Q
  msg->setAdvancedProtocolString(session_.alpn_);
  msg->setSecure(true);
  CHECK(codecStreamId_);
  CHECK_EQ(streamID, *codecStreamId_);

  session_.setupOnHeadersComplete(&txn_, msg.get());
  if (!txn_.getHandler()) {
    txn_.sendAbort();
    return;
  }

  // for h1q-fb-v1 start draining on receipt of a Connection:: close header
  // if we are getting a response, transportReady has been called!
  DCHECK(session_.versionUtils_);
  session_.versionUtils_->headersComplete(msg.get());

  // onHeadersComplete can be triggered by data from a different stream ID
  // - specifically, the QPACK encoder stream.  If that's true, then there may
  // be unparsed data in HQStreamTransport.  Add this stream's id to the
  // read set and schedule a loop callback to restart it.
  if (session_.pendingProcessReadSet_.find(getStreamId()) ==
          session_.pendingProcessReadSet_.end() &&
      !readBuf_.empty()) {
    session_.pendingProcessReadSet_.insert(getStreamId());
    session_.scheduleLoopCallback();
  }

  // Tell the HTTPTransaction to start processing the message now
  // that the full ingress headers have arrived.
  txn_.onIngressHeadersComplete(std::move(msg));

  auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - createdTime);
  QUIC_TRACE_SOCK(stream_event,
                  session_.sock_,
                  "on_headers",
                  getStreamId(),
                  (uint64_t)timeDiff.count());
}

void HQSession::HQStreamTransportBase::transactionTimeout(
    HTTPTransaction* txn) noexcept {
  auto g = folly::makeGuard(setActiveCodec(__func__));
  VLOG(4) << __func__ << " txn=" << txn_;
  DCHECK(txn == &txn_);
  // A transaction has timed out.  If the transaction does not have
  // a Handler yet, because we haven't yet received the full request
  // headers, we give it a DirectResponseHandler that generates an
  // error page.
  VLOG(3) << "Transaction timeout for streamID=" << getStreamId();

  if (!codecStreamId_) {
    // transactionTimeout before onMessageBegin
    codecStreamId_ = codecFilterChain->createStream();
  }

  if (!txn_.getHandler() &&
      txn_.getEgressState() == HTTPTransactionEgressSM::State::Start) {
    VLOG(4) << " Timed out receiving headers. " << this;
    if (session_.infoCallback_) {
      session_.infoCallback_->onIngressError(session_, kErrorTimeout);
    }

    VLOG(4) << " creating direct error handler. " << this;
    auto handler = session_.getTransactionTimeoutHandler(&txn_);
    txn_.setHandler(handler);
  }

  // There may be unparsed ingress.  Discard it.
  abortIngress();

  // Tell the transaction about the timeout.  The transaction will
  // communicate the timeout to the handler, and the handler will
  // decide how to proceed.
  session_.abortStream(HTTPException::Direction::INGRESS,
                       getStreamId(),
                       HTTP3::ErrorCode::HTTP_INTERNAL_ERROR);
  txn_.onIngressTimeout();
}

void HQSession::abortStream(HTTPException::Direction dir,
                            quic::StreamId id,
                            HTTP3::ErrorCode err) {
  CHECK(sock_);
  if (dir != HTTPException::Direction::EGRESS &&
      (sock_->isBidirectionalStream(id) || isPeerUniStream(id))) {
    // Any INGRESS abort generates a QPACK cancel
    versionUtils_->abortStream(id);
    sock_->stopSending(id, err);
  }
  if (dir != HTTPException::Direction::INGRESS &&
      (sock_->isBidirectionalStream(id) || isSelfUniStream(id))) {
    sock_->resetStream(id, err);
  }
}

void HQSession::HQVersionUtils::abortStream(quic::StreamId id) {
  auto cancel = qpackCodec_.encodeCancelStream(id);
  auto QPACKDecoderStream =
      session_.findControlStream(hq::UnidirectionalStreamType::QPACK_DECODER);
  DCHECK(QPACKDecoderStream);
  QPACKDecoderStream->writeBuf_.append(std::move(cancel));
  session_.scheduleWrite();
}

void HQSession::HQStreamTransportBase::sendHeaders(HTTPTransaction* txn,
                                                   const HTTPMessage& headers,
                                                   HTTPHeaderSize* size,
                                                   bool includeEOM) noexcept {
  VLOG(4) << __func__ << " txn=" << txn_;
  DCHECK(txn == &txn_);

  if (session_.versionUtils_) {
    // for h1q-fb-v1 initiate shutdown when sending a request
    // a good client should always wait for onTransportReady before sending
    // data
    session_.versionUtils_->checkSendingGoaway(headers);
  }

  const uint64_t oldOffset = streamByteOffset();
  auto g = folly::makeGuard(setActiveCodec(__func__));
  CHECK(codecStreamId_);
  if (headers.isRequest() && txn->getAssocTxnId()) {
    codecFilterChain->generatePushPromise(writeBuf_,
                                          *codecStreamId_,
                                          headers,
                                          *txn->getAssocTxnId(),
                                          includeEOM,
                                          size);
  } else {
    codecFilterChain->generateHeader(
        writeBuf_, *codecStreamId_, headers, includeEOM, size);
  }
  const uint64_t newOffset = streamByteOffset();
  if (size) {
    VLOG(3) << "sending headers, size=" << size->compressed
            << ", uncompressedSize=" << size->uncompressed << " txn=" << txn_;
  }

  // only do it for downstream now to bypass handling upstream reuse cases
  if (/*isDownstream() &&*/ headers.isResponse() && newOffset > oldOffset &&
      // catch 100-ish response?
      !txn->testAndSetFirstHeaderByteSent()) {
    byteEventTracker_.addFirstHeaderByteEvent(newOffset, txn);
  }

  if (includeEOM) {
    CHECK_GE(newOffset, oldOffset);
    session_.handleLastByteEvents(&byteEventTracker_,
                                  &txn_,
                                  newOffset - oldOffset,
                                  streamByteOffset(),
                                  true);
  }
  pendingEOM_ = includeEOM;
  notifyPendingEgress();

  auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - createdTime);
  QUIC_TRACE_SOCK(stream_event,
                  session_.sock_,
                  "headers",
                  getStreamId(),
                  (uint64_t)timeDiff.count());
  if (includeEOM) {
    QUIC_TRACE_SOCK(stream_event,
                    session_.sock_,
                    "eom",
                    getStreamId(),
                    (uint64_t)timeDiff.count());
  }
}

size_t HQSession::HQStreamTransportBase::sendEOM(
    HTTPTransaction* txn, const HTTPHeaders* trailers) noexcept {
  VLOG(4) << __func__ << " txn=" << txn_;
  DCHECK(txn == &txn_);
  auto g = folly::makeGuard(setActiveCodec(__func__));

  size_t encodedSize = 0;

  CHECK(codecStreamId_);
  if (trailers) {
    encodedSize = codecFilterChain->generateTrailers(
        writeBuf_, *codecStreamId_, *trailers);
  }

  encodedSize += codecFilterChain->generateEOM(writeBuf_, *codecStreamId_);

  // This will suppress the call to onEgressBodyLastByte in
  // handleLastByteEvents, since we're going to add a last byte event anyways.
  // This safely keeps the txn open until we egress the FIN to the transport.
  // At that point, the deliveryCallback should also be registered.
  // Note: even if the byteEventTracker_ is already at streamByteOffset(),
  // it is still invoked with the same offset after egressing the FIN.
  bool pretendPiggybacked = (encodedSize == 0);
  session_.handleLastByteEvents(&byteEventTracker_,
                                &txn_,
                                encodedSize,
                                streamByteOffset(),
                                pretendPiggybacked);
  if (pretendPiggybacked) {
    byteEventTracker_.addLastByteEvent(txn, streamByteOffset());
  }
  // For H1 without chunked transfer-encoding, generateEOM is a no-op
  // We need to make sure writeChain(eom=true) gets called
  pendingEOM_ = true;
  notifyPendingEgress();
  auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - createdTime);
  QUIC_TRACE_SOCK(stream_event,
                  session_.sock_,
                  "eom",
                  getStreamId(),
                  (uint64_t)timeDiff.count());
  return encodedSize;
}

size_t HQSession::HQStreamTransportBase::sendAbort(
    HTTPTransaction* /*txn*/, ErrorCode errorCode) noexcept {
  return sendAbortImpl(toHTTP3ErrorCode(errorCode),
                       folly::to<std::string>("Application aborts, errorCode=",
                                              getErrorCodeString(errorCode)));
}

size_t HQSession::HQStreamTransportBase::sendAbortImpl(HTTP3::ErrorCode code,
                                                       std::string errorMsg) {
  VLOG(3) << __func__ << " txn=" << txn_;
  session_.abortStream(
      HTTPException::Direction::INGRESS_AND_EGRESS, getStreamId(), code);

  abortEgress(true);
  // We generated 0 application bytes so return 0?
  auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - createdTime);
  QUIC_TRACE_SOCK(stream_event,
                  session_.sock_,
                  "abort",
                  getStreamId(),
                  timeDiff.count(),
                  errorMsg);
  return 0;
}

void HQSession::HQStreamTransportBase::abortIngress() {
  VLOG(4) << "Aborting ingress for " << txn_;
  ingressError_ = true;
  readBuf_.move();
  codecFilterChain->setParserPaused(true);
}

void HQSession::HQStreamTransportBase::abortEgress(bool checkForDetach) {
  VLOG(4) << "Aborting egress for " << txn_;
  byteEventTracker_.drainByteEvents();
  writeBuf_.move();
  pendingEOM_ = false;
  if (queueHandle_.isStreamTransportEnqueued()) {
    VLOG(4) << "clearPendingEgress for " << txn_;
    session_.txnEgressQueue_.clearPendingEgress(queueHandle_.getHandle());
  }
  if (checkForDetach) {
    HTTPTransaction::DestructorGuard dg(&txn_);
  }
}

void HQSession::HQControlStream::onError(HTTPCodec::StreamID streamID,
                                         const HTTPException& error,
                                         bool /* newTxn */) {
  // All the errors on the control stream are to be considered session errors
  // anyway, so just use the ingress stream id
  if (streamID == kSessionStreamId) {
    streamID = getIngressStreamId();
  }
  session_.handleSessionError(
      CHECK_NOTNULL(session_.findControlStream(streamID)),
      StreamDirection::INGRESS,
      toHTTP3ErrorCode(error),
      kErrorConnection);
}

void HQSession::HQStreamTransportBase::onError(HTTPCodec::StreamID streamID,
                                               const HTTPException& error,
                                               bool /* newTxn */) {
  VLOG(4) << __func__ << " (from Codec) txn=" << txn_ << " err=" << error;
  // Codec must either call onMessageComplete or onError, but not both
  // I think.  The exception might be if stream has more than one HTTP
  // message on it.
  CHECK(!eomGate_.get(EOMType::CODEC));
  ingressError_ = true;

  if (streamID == kSessionStreamId) {
    session_.handleSessionError(
        this, StreamDirection::INGRESS, toHTTP3ErrorCode(error),
        kErrorConnection);
    return;
  }

  if (!codecStreamId_ && error.hasHttpStatusCode() && streamID != 0) {
    // onError before onMessageBegin
    codecStreamId_ = streamID;
  }

  if (!txn_.getHandler() &&
      txn_.getEgressState() == HTTPTransactionEgressSM::State::Start) {
    session_.handleErrorDirectly(&txn_, error);
    return;
  }

  txn_.onError(error);
  auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - createdTime);
  QUIC_TRACE_SOCK(stream_event,
                  session_.sock_,
                  "on_error",
                  getStreamId(),
                  (uint64_t)timeDiff.count());
}

void HQSession::HQStreamTransportBase::onResetStream(HTTP3::ErrorCode errorCode,
                                                     HTTPException ex) {
  // kErrorStreamAbort prevents HTTPTransaction from calling sendAbort in reply.
  // We use this code and manually call sendAbort here for appropriate cases
  HTTP3::ErrorCode replyError;
  if (session_.direction_ == TransportDirection::UPSTREAM) {
    // Upstream ingress closed - cancel this request.
    replyError = HTTP3::ErrorCode::HTTP_REQUEST_CANCELLED;
  } else if (!txn_.isIngressStarted()) {
    // Downstream ingress closed with no ingress yet, we can send REJECTED
    // It's actually ok if we've received headers but not made any
    // calls to the handler, but there's no API for that.
    replyError = HTTP3::ErrorCode::HTTP_REQUEST_REJECTED;
  } else {
    // Downstream ingress closed but we've received some ingress.
    // TODO: This can be HTTP_REQUEST_CANCELLED also after the next release
    // Does it require hq-04 to prevent clients from retrying accidentally?
    replyError = HTTP3::ErrorCode::HTTP_NO_ERROR;
  }

  if (errorCode == HTTP3::ErrorCode::HTTP_REQUEST_REJECTED) {
    VLOG_IF(2, session_.direction_ == TransportDirection::DOWNSTREAM)
        << "RST_STREAM/REJECTED should not be sent by clients txn=" << txn_;
    // kErrorStreamUnacknowledged signals that this is safe to retry
    ex.setProxygenError(kErrorStreamUnacknowledged);
  } else {
    ex.setProxygenError(kErrorStreamAbort);
  }
  if (errorCode == HTTP3::ErrorCode::GIVEUP_ZERO_RTT) {
    // This error code comes from application who wants to error out all
    // transactions over hqsession because QUIC lost race with TCP. Passing this
    // error back to transactions through onError so that they can be retried.
    ex.setProxygenError(kErrorEarlyDataFailed);
  }
  // TODO: set Quic error when quic is OSS
  ex.setErrno(uint32_t(errorCode));
  auto msg = ex.what();
  errorOnTransaction(std::move(ex));
  sendAbortImpl(replyError, msg);
}

void HQSession::HQStreamTransportBase::notifyPendingEgress() noexcept {
  VLOG(4) << __func__ << " txn=" << txn_;
  signalPendingEgressImpl();
  session_.scheduleWrite();
}

size_t HQSession::HQStreamTransportBase::sendBody(
    HTTPTransaction* txn,
    std::unique_ptr<folly::IOBuf> body,
    bool includeEOM,
    bool /* unused */) noexcept {
  VLOG(4) << __func__ << " len=" << body->computeChainDataLength()
          << " eof=" << includeEOM << " txn=" << txn_;
  DCHECK(txn == &txn_);
  uint64_t offset = streamByteOffset();
  auto g = folly::makeGuard(setActiveCodec(__func__));
  CHECK(codecStreamId_);
  size_t encodedSize = codecFilterChain->generateBody(writeBuf_,
                                                      *codecStreamId_,
                                                      std::move(body),
                                                      HTTPCodec::NoPadding,
                                                      includeEOM);
  if (encodedSize > 0 && !txn->testAndSetFirstByteSent()) {
    byteEventTracker_.addFirstBodyByteEvent(offset + 1, txn);
  }

  if (includeEOM) {
    session_.handleLastByteEvents(
        &byteEventTracker_, &txn_, encodedSize, streamByteOffset(), true);
    VLOG(3) << "sending EOM in body for streamID=" << getStreamId()
            << " txn=" << txn_;
    pendingEOM_ = true;
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - createdTime);
    QUIC_TRACE_SOCK(stream_event,
                    session_.sock_,
                    "eom",
                    getStreamId(),
                    (uint64_t)timeDiff.count());
  }
  return encodedSize;
}

size_t HQSession::HQStreamTransportBase::sendChunkHeader(
    HTTPTransaction* txn, size_t length) noexcept {
  VLOG(4) << __func__ << " txn=" << txn_;
  DCHECK(txn == &txn_);
  auto g = folly::makeGuard(setActiveCodec(__func__));
  CHECK(codecStreamId_);
  size_t encodedSize =
      codecFilterChain->generateChunkHeader(writeBuf_, *codecStreamId_, length);
  notifyPendingEgress();
  return encodedSize;
}

size_t HQSession::HQStreamTransportBase::sendChunkTerminator(
    HTTPTransaction* txn) noexcept {
  VLOG(4) << __func__ << " txn=" << txn_;
  DCHECK(txn == &txn_);
  auto g = folly::makeGuard(setActiveCodec(__func__));
  CHECK(codecStreamId_);
  size_t encodedSize =
      codecFilterChain->generateChunkTerminator(writeBuf_, *codecStreamId_);
  notifyPendingEgress();
  return encodedSize;
}

void HQSession::HQStreamTransportBase::onMessageBegin(
    HTTPCodec::StreamID streamID, HTTPMessage* /* msg */) {
  VLOG(4) << __func__ << " txn=" << txn_;
  if (session_.infoCallback_) {
    session_.infoCallback_->onRequestBegin(session_);
  }
  // NOTE: for H2 this is where we create a new stream and transaction.
  // for HQ there is nothing to do here, except caching the codec streamID
  codecStreamId_ = streamID;
}

std::ostream& operator<<(std::ostream& os, const HQSession& session) {
  session.describe(os);
  return os;
}

} // namespace proxygen
