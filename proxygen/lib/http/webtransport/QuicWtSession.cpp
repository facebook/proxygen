/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/webtransport/QuicWtSession.h>

using namespace proxygen;
using namespace proxygen::detail;
using FCState = WebTransport::FCState;

namespace {
static constexpr uint64_t kMaxWtIngressBuf = 65'535;

WtStreamManager::WtConfig createQuicConfig() {
  WtStreamManager::WtConfig config;
  config.selfMaxStreamsBidi = kMaxVarint;
  config.selfMaxStreamsUni = kMaxVarint;
  config.selfMaxConnData = kMaxVarint;
  config.selfMaxStreamDataBidi = kMaxVarint;
  config.selfMaxStreamDataUni = kMaxVarint;
  config.peerMaxStreamsBidi = kMaxVarint;
  config.peerMaxStreamsUni = kMaxVarint;
  config.peerMaxConnData = kMaxVarint;
  config.peerMaxStreamDataBidi = kMaxVarint;
  config.peerMaxStreamDataUni = kMaxVarint;
  return config;
}

struct QuicWtEventVisitor {
  quic::QuicSocket& quicSocket;

  void operator()(const WtStreamManager::ResetStream& ev) const {
    quicSocket.resetStream(ev.streamId, ev.err);
  }

  void operator()(const WtStreamManager::StopSending& ev) const {
    quicSocket.setReadCallback(ev.streamId, nullptr, ev.err);
  }

  void operator()(const WtStreamManager::CloseSession& /*ev*/) const {
    // Don't call wtHandler_->onSessionEnd here, it's handled in
    // onConnectionEndImpl. WtStreamManager generates CloseSession when
    // we call shutdown(), which already means we're closing the session.
  }

  void operator()(const WtStreamManager::MaxConnData& /*ev*/) const {
  }
  void operator()(const WtStreamManager::MaxStreamData& /*ev*/) const {
  }
  void operator()(const WtStreamManager::MaxStreamsBidi& /*ev*/) const {
  }
  void operator()(const WtStreamManager::MaxStreamsUni& /*ev*/) const {
  }
  void operator()(const WtStreamManager::DrainSession& /*ev*/) const {
  }
};

uint64_t getQuicAppErrCode(const QuicError& err) noexcept {
  auto* appEc = err.code.asApplicationErrorCode();
  return appEc ? *appEc : 0;
}

} // namespace

namespace proxygen {

QuicWtSessionBase::QuicWtSessionBase(
    std::shared_ptr<quic::QuicSocket> quicSocket,
    std::unique_ptr<WebTransportHandler> wtHandler,
    WtStreamManager::WtConfig wtConfig)
    : WtSessionBase(nullptr, sm_),
      quicSocket_(std::move(quicSocket)),
      wtHandler_(std::move(wtHandler)),
      priorityQueue_(std::make_unique<quic::HTTPPriorityQueue>()),
      sm_{quicSocket_->getNodeType() == quic::QuicNodeType::Server
              ? detail::WtDir::Server
              : detail::WtDir::Client,
          wtConfig,
          smCb_,
          smCb_,
          *priorityQueue_} {
}

QuicWtSessionBase::~QuicWtSessionBase() {
  QuicWtSessionBase::closeSession(folly::none);
}

folly::Expected<WebTransport::StreamWriteHandle*, WebTransport::ErrorCode>
QuicWtSessionBase::createUniStream() noexcept {
  XCHECK(quicSocket_);
  auto id = quicSocket_->createUnidirectionalStream();
  if (id.hasError()) {
    return folly::makeUnexpected(ErrorCode::STREAM_CREATION_ERROR);
  }
  return CHECK_NOTNULL(sm_.getOrCreateEgressHandle(*id));
}

folly::Expected<WebTransport::BidiStreamHandle, WebTransport::ErrorCode>
QuicWtSessionBase::createBidiStream() noexcept {
  XCHECK(quicSocket_);
  auto id = quicSocket_->createBidirectionalStream();
  if (id.hasError()) {
    return folly::makeUnexpected(ErrorCode::STREAM_CREATION_ERROR);
  }
  auto bidiHandle = sm_.getOrCreateBidiHandle(*id);
  XCHECK(bidiHandle.readHandle && bidiHandle.writeHandle);
  sm_.setReadCb(*bidiHandle.readHandle, &smCb_);
  quicSocket_->setReadCallback(*id, &readCb_);
  return bidiHandle;
}

folly::SemiFuture<folly::Unit>
QuicWtSessionBase::awaitUniStreamCredit() noexcept {
  XCHECK(quicSocket_);
  if (quicSocket_->getNumOpenableUnidirectionalStreams() > 0) {
    return folly::makeFuture(folly::unit);
  }
  auto [promise, future] = folly::makePromiseContract<folly::Unit>();
  uniCreditPromise() = std::move(promise);
  return std::move(future);
}

folly::SemiFuture<folly::Unit>
QuicWtSessionBase::awaitBidiStreamCredit() noexcept {
  XCHECK(quicSocket_);
  if (quicSocket_->getNumOpenableBidirectionalStreams() > 0) {
    return folly::makeFuture(folly::unit);
  }
  auto [promise, future] = folly::makePromiseContract<folly::Unit>();
  bidiCreditPromise() = std::move(promise);
  return std::move(future);
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSessionBase::sendDatagram(IoBufPtr datagram) noexcept {
  XCHECK(quicSocket_);
  auto writeRes = quicSocket_->writeDatagram(std::move(datagram));
  if (writeRes.hasError()) {
    XLOG(ERR) << __func__ << "; err= " << writeRes.error();
    return folly::makeUnexpected(ErrorCode::GENERIC_ERROR);
  }
  return folly::unit;
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSessionBase::closeSession(folly::Optional<uint32_t> error) noexcept {
  sm_.shutdown({.err = error.value_or(0), .msg = "closeSession"});
  if (auto wtHandler = std::exchange(wtHandler_, nullptr)) {
    wtHandler->onSessionEnd(error);
  }
  return folly::unit;
}

void QuicWtSessionBase::onDatagram(IoBufPtr dgram) noexcept {
  if (wtHandler_) {
    wtHandler_->onDatagram(std::move(dgram));
  }
}

// -- QuicReadCallback overrides --
void QuicWtSessionBase::QuicReadCallback::readAvailable(StreamId id) noexcept {
  XCHECK(sess.quicSocket_);
  auto& sm = sess.sm_;
  auto& quicSocket = sess.quicSocket_;

  auto* rh = sm.getBidiHandle(id).readHandle;
  if (!rh) {
    XLOG(ERR) << "nullptr rh id=" << id;
    return;
  }
  auto canRead =
      kMaxWtIngressBuf - std::min(sm.bufferedBytes(*rh), kMaxWtIngressBuf);
  if (canRead == 0) {
    sess.maybePauseIngress(*rh);
    return;
  }
  auto readRes = quicSocket->read(id, canRead);
  if (readRes.hasError()) {
    XLOG(ERR) << "::read err id=" << id;
    return;
  }
  auto& [data, eof] = readRes.value();
  auto res = sm.enqueue(
      *rh, WebTransport::StreamData{.data = std::move(data), .fin = eof});
  XCHECK_NE(res, detail::WtStreamManager::Result::Fail);
  if (!eof) { // ::enqueue w/ eof=true may deallocate rh
    sess.maybePauseIngress(*rh);
  }
}

void QuicWtSessionBase::QuicReadCallback::readError(StreamId id,
                                                    QuicError error) noexcept {
  XLOG(ERR) << __func__ << "; id=" << id << "; err=" << error;
  sess.sm_.onResetStream(detail::WtStreamManager::ResetStream{
      id, *error.code.asApplicationErrorCode()});
}

// -- StreamManagerCallback overrides --
void QuicWtSessionBase::StreamManagerCallback::readReady(
    detail::WtStreamManager::WtReadHandle& rh) noexcept {
  sess.maybeResumeIngress(rh);
}

void QuicWtSessionBase::StreamManagerCallback::eventsAvailable() noexcept {
  XCHECK(sess.quicSocket_);
  // process control events first
  auto events = sess.sm_.moveEvents();
  for (auto& event : events) {
    std::visit(QuicWtEventVisitor{*sess.quicSocket_}, event);
  }
  // then process writable streams
  while (!sess.priorityQueue_->empty()) {
    auto id = sess.priorityQueue_->getNextScheduledID(std::nullopt);
    if (!id.isStreamID()) { // skip datagrams
      break;
    }
    auto streamId = id.asStreamID();
    auto maxData = sess.quicSocket_->getMaxWritableOnStream(streamId);
    auto* wh = sess.sm_.getBidiHandle(streamId).writeHandle;
    if (!wh || !maxData) {
      XLOG(DBG4) << "nullptr wh id=" << streamId;
      sess.priorityQueue_->erase(id);
      continue;
    }
    if (*maxData == 0) {
      XLOG(DBG4) << "egress conn-fc blocked id=" << streamId;
      sess.priorityQueue_->erase(id);
      sess.quicSocket_->notifyPendingWriteOnStream(streamId, &sess);
      continue;
    }
    auto streamData = sess.sm_.dequeue(*wh, *maxData);
    if (streamData.data || streamData.fin) {
      auto res = sess.quicSocket_->writeChain(streamId,
                                              std::move(streamData.data),
                                              streamData.fin,
                                              streamData.deliveryCallback);
      if (res.hasError()) {
        XLOG(ERR) << "QuicSocket::writeChain err id=" << streamId;
        wh->resetStream(WebTransport::kInternalError);
      }
    }
  }
}

void QuicWtSessionBase::StreamManagerCallback::onNewPeerStream(
    uint64_t /*streamId*/) noexcept {
}

// -- StreamWriteCallback overrides --
void QuicWtSessionBase::onStreamWriteReady(quic::StreamId streamId,
                                           uint64_t /*maxToSend*/) noexcept {
  auto* wh = sm_.getBidiHandle(streamId).writeHandle;
  if (wh) {
    priorityQueue_->insertOrUpdate(
        quic::PriorityQueue::Identifier::fromStreamID(wh->getID()),
        wh->getPriority());
    smCb_.eventsAvailable();
  }
}

void QuicWtSessionBase::onStreamWriteError(quic::StreamId id,
                                           QuicError error) noexcept {
  XLOG(ERR) << __func__ << "; id=" << id << "; err=" << error;
  if (auto* wh = sm_.getBidiHandle(id).writeHandle) {
    wh->resetStream(WebTransport::kInternalError);
  }
}

void QuicWtSessionBase::maybePauseIngress(
    detail::WtStreamManager::WtReadHandle& handle) noexcept {
  XCHECK(quicSocket_);
  const auto id = handle.getID();
  if (sm_.bufferedBytes(handle) >= kMaxWtIngressBuf) {
    XLOG(DBG4) << __func__ << "; id=" << id;
    auto res = quicSocket_->pauseRead(id);
    XLOG_IF(ERR, res.hasError()) << __func__ << "; err id=" << id;
  }
}

void QuicWtSessionBase::maybeResumeIngress(
    detail::WtStreamManager::WtReadHandle& handle) noexcept {
  XCHECK(quicSocket_);
  const auto id = handle.getID();
  if (sm_.bufferedBytes(handle) < kMaxWtIngressBuf) {
    XLOG(DBG4) << __func__ << "; id=" << id;
    auto res = quicSocket_->resumeRead(id);
    XLOG_IF(ERR, res.hasError()) << __func__ << "; err id=" << id;
  }
}

/**
 * QuicWtSession implementation below. Most of the functionality is shared with
 * QuicWtSessionBase - however this derived class assumes ownership of all of
 * QuicSocket's streams, so it owns and installs a
 * QuicConnectionCallback. Similarly the destructor/::closeSession closes all
 * streams on the underlying QuicSocket via QuicSocket::close.
 */
QuicWtSession::QuicWtSession(std::shared_ptr<quic::QuicSocket> quicSocket,
                             std::unique_ptr<WebTransportHandler> wtHandler)
    : QuicWtSessionBase(
          std::move(quicSocket), std::move(wtHandler), createQuicConfig()) {
  quicSocket_->setConnectionCallback(&connCb_);
  quicSocket_->setDatagramCallback(&dgramCb_);
}

QuicWtSession::~QuicWtSession() {
  QuicWtSession::closeSession(folly::none);
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSession::closeSession(folly::Optional<uint32_t> error) noexcept {
  QuicWtSessionBase::closeSession(error);
  quicSocket_->setConnectionCallback(nullptr);
  quicSocket_->setDatagramCallback(nullptr);
  quicSocket_->close(QuicError(quic::ApplicationErrorCode(error.value_or(0))));
  return folly::unit;
}

// -- QuicConnectionCallback overrides --
void QuicWtSession::QuicConnectionCallback::onNewBidirectionalStream(
    StreamId id) noexcept {
  XCHECK(sess.wtHandler_);
  auto bidiHandle = sess.sm_.getOrCreateBidiHandle(id);
  XCHECK(bidiHandle.readHandle && bidiHandle.writeHandle);
  sess.sm_.setReadCb(*bidiHandle.readHandle, &sess.smCb_);
  sess.quicSocket_->setReadCallback(id, &sess.readCb_);
  sess.wtHandler_->onNewBidiStream(bidiHandle);
}

void QuicWtSession::QuicConnectionCallback::onNewUnidirectionalStream(
    StreamId id) noexcept {
  XCHECK(sess.wtHandler_);
  auto* rh = CHECK_NOTNULL(sess.sm_.getOrCreateIngressHandle(id));
  sess.sm_.setReadCb(*rh, &sess.smCb_);
  sess.quicSocket_->setReadCallback(id, &sess.readCb_);
  sess.wtHandler_->onNewUniStream(rh);
}

void QuicWtSession::QuicConnectionCallback::onStopSending(
    StreamId id, quic::ApplicationErrorCode errorCode) noexcept {
  sess.sm_.onStopSending({.streamId = id, .err = errorCode});
}

void QuicWtSession::QuicConnectionCallback::onConnectionEnd() noexcept {
  sess.closeSession(folly::none);
}

void QuicWtSession::QuicConnectionCallback::onConnectionEnd(
    QuicError error) noexcept {
  sess.closeSession(getQuicAppErrCode(error));
}

void QuicWtSession::QuicConnectionCallback::onConnectionError(
    QuicError error) noexcept {
  sess.closeSession(getQuicAppErrCode(error));
}

void QuicWtSession::QuicConnectionCallback::onBidirectionalStreamsAvailable(
    uint64_t numStreamsAvailable) noexcept {
  if (numStreamsAvailable > 0) {
    sess.onBidiStreamCreditAvail();
  }
}

void QuicWtSession::QuicConnectionCallback::onUnidirectionalStreamsAvailable(
    uint64_t numStreamsAvailable) noexcept {
  if (numStreamsAvailable > 0) {
    sess.onUniStreamCreditAvail();
  }
}

void QuicWtSession::QuicDgramCallback::onDatagramsAvailable() noexcept {
  XCHECK(sess.quicSocket_);
  auto& quicSocket = sess.quicSocket_;

  auto result = quicSocket->readDatagramBufs();
  if (result.hasError()) {
    XLOG(ERR) << __func__ << "; err=" << toString(result.error());
    sess.closeSession(WebTransport::kInternalError);
    return;
  }

  XLOG(DBG4) << "rx nDatagrams=" << result->size();
  for (auto& dgram : result.value()) {
    sess.onDatagram(std::move(dgram));
  }
}

} // namespace proxygen
