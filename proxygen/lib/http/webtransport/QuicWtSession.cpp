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

WtStreamManager::WtConfig createConfig() {
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

} // namespace

namespace proxygen {

QuicWtSession::QuicWtSession(std::shared_ptr<quic::QuicSocket> quicSocket,
                             std::unique_ptr<WebTransportHandler> wtHandler)
    : WtSessionBase(nullptr, sm_),
      quicSocket_(std::move(quicSocket)),
      wtHandler_(std::move(wtHandler)),
      priorityQueue_(std::make_unique<quic::HTTPPriorityQueue>()),
      sm_{quicSocket_->getState()->nodeType == quic::QuicNodeType::Server
              ? detail::WtDir::Server
              : detail::WtDir::Client,
          createConfig(),
          *this,
          *this,
          *priorityQueue_} {
  quicSocket_->setConnectionCallback(&connCb_);
  quicSocket_->setDatagramCallback(this);
}

QuicWtSession::~QuicWtSession() {
  closeSessionImpl(folly::none);
  quicSocket_.reset();
  wtHandler_.reset();
}

folly::Expected<WebTransport::StreamWriteHandle*, WebTransport::ErrorCode>
QuicWtSession::createUniStream() noexcept {
  XCHECK(quicSocket_);
  auto id = quicSocket_->createUnidirectionalStream();
  if (id.hasError()) {
    return folly::makeUnexpected(ErrorCode::STREAM_CREATION_ERROR);
  }
  return CHECK_NOTNULL(sm_.getOrCreateEgressHandle(*id));
}

folly::Expected<WebTransport::BidiStreamHandle, WebTransport::ErrorCode>
QuicWtSession::createBidiStream() noexcept {
  XCHECK(quicSocket_);
  auto id = quicSocket_->createBidirectionalStream();
  if (id.hasError()) {
    return folly::makeUnexpected(ErrorCode::STREAM_CREATION_ERROR);
  }
  auto bidiHandle = sm_.getOrCreateBidiHandle(*id);
  XCHECK(bidiHandle.readHandle && bidiHandle.writeHandle);
  sm_.setReadCb(*bidiHandle.readHandle, this);
  quicSocket_->setReadCallback(*id, &readCb_);
  return bidiHandle;
}

folly::SemiFuture<folly::Unit> QuicWtSession::awaitUniStreamCredit() noexcept {
  XCHECK(quicSocket_);
  if (quicSocket_->getNumOpenableUnidirectionalStreams() > 0) {
    return folly::makeFuture(folly::unit);
  }
  auto [promise, future] = folly::makePromiseContract<folly::Unit>();
  uniCreditPromise() = std::move(promise);
  return std::move(future);
}

folly::SemiFuture<folly::Unit> QuicWtSession::awaitBidiStreamCredit() noexcept {
  XCHECK(quicSocket_);
  if (quicSocket_->getNumOpenableBidirectionalStreams() > 0) {
    return folly::makeFuture(folly::unit);
  }
  auto [promise, future] = folly::makePromiseContract<folly::Unit>();
  bidiCreditPromise() = std::move(promise);
  return std::move(future);
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSession::sendDatagram(std::unique_ptr<folly::IOBuf> datagram) noexcept {
  XCHECK(quicSocket_);
  auto writeRes = quicSocket_->writeDatagram(std::move(datagram));
  if (writeRes.hasError()) {
    XLOG(ERR) << "Failed to send datagram, err= " << writeRes.error();
    return folly::makeUnexpected(ErrorCode::GENERIC_ERROR);
  }
  return folly::unit;
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSession::closeSession(folly::Optional<uint32_t> error) noexcept {
  return closeSessionImpl(std::move(error));
}

// -- QuicReadCallback overrides --
void QuicWtSession::QuicReadCallback::readAvailable(StreamId id) noexcept {
  XCHECK(sess.quicSocket_);
  auto& sm = sess.sm_;
  auto& quicSocket = sess.quicSocket_;

  auto* rh = sm.getBidiHandle(id).readHandle;
  if (!rh) {
    XLOG(ERR) << "Read handle not found for stream " << id;
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
    XLOG(ERR) << "Read error for stream " << id;
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

void QuicWtSession::QuicReadCallback::readError(StreamId id,
                                                QuicError error) noexcept {
  XLOG(ERR) << "Read error on stream " << id << ": " << error;
  sess.sm_.onResetStream(detail::WtStreamManager::ResetStream{
      id, *error.code.asApplicationErrorCode()});
}

// -- QuicConnectionCallback overrides --
void QuicWtSession::QuicConnectionCallback::onNewBidirectionalStream(
    StreamId id) noexcept {
  XCHECK(sess.wtHandler_);
  auto bidiHandle = sess.sm_.getOrCreateBidiHandle(id);
  XCHECK(bidiHandle.readHandle && bidiHandle.writeHandle);
  sess.sm_.setReadCb(*bidiHandle.readHandle, &sess);
  sess.quicSocket_->setReadCallback(id, &sess.readCb_);
  sess.wtHandler_->onNewBidiStream(bidiHandle);
}

void QuicWtSession::QuicConnectionCallback::onNewUnidirectionalStream(
    StreamId id) noexcept {
  XCHECK(sess.wtHandler_);
  auto* rh = CHECK_NOTNULL(sess.sm_.getOrCreateIngressHandle(id));
  sess.sm_.setReadCb(*rh, &sess);
  sess.quicSocket_->setReadCallback(id, &sess.readCb_);
  sess.wtHandler_->onNewUniStream(rh);
}

void QuicWtSession::QuicConnectionCallback::onStopSending(
    StreamId id, quic::ApplicationErrorCode errorCode) noexcept {
  sess.sm_.onStopSending({.streamId = id, .err = errorCode});
}

void QuicWtSession::QuicConnectionCallback::onConnectionEnd() noexcept {
  sess.onConnectionEndImpl(folly::none);
}

void QuicWtSession::QuicConnectionCallback::onConnectionEnd(
    QuicError error) noexcept {
  sess.onConnectionEndImpl(error);
}

void QuicWtSession::QuicConnectionCallback::onConnectionError(
    QuicError error) noexcept {
  sess.onConnectionEndImpl(error);
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

// -- QuicSocket::DatagramCallback overrides --
void QuicWtSession::onDatagramsAvailable() noexcept {
  XCHECK(quicSocket_);
  auto result = quicSocket_->readDatagramBufs();
  if (result.hasError()) {
    XLOG(ERR) << "Got error while reading datagrams, err="
              << toString(result.error());
    closeSession(WebTransport::kInternalError);
    return;
  }
  XLOG(DBG4) << "Received " << result.value().size() << " datagrams";
  XCHECK(wtHandler_);
  for (auto& datagram : result.value()) {
    if (wtHandler_) {
      wtHandler_->onDatagram(std::move(datagram));
    }
  }
}

// -- WtStreamManager::ReadCallback overrides --
void QuicWtSession::readReady(
    detail::WtStreamManager::WtReadHandle& rh) noexcept {
  maybeResumeIngress(rh);
}

// -- WtStreamManager::EgressCallback overrides --
void QuicWtSession::eventsAvailable() noexcept {
  XCHECK(quicSocket_);
  // process control events first
  auto events = sm_.moveEvents();
  for (auto& event : events) {
    std::visit(QuicWtEventVisitor{*quicSocket_}, event);
  }
  // then process writable streams
  while (!priorityQueue_->empty()) {
    auto id = priorityQueue_->getNextScheduledID(std::nullopt);
    if (!id.isStreamID()) { // skip datagrams
      break;
    }
    auto streamId = id.asStreamID();
    auto maxData = quicSocket_->getMaxWritableOnStream(streamId);
    auto* wh = sm_.getBidiHandle(streamId).writeHandle;
    if (!wh || !maxData) {
      XLOG(DBG4) << "Write handle or stream not found for " << streamId;
      priorityQueue_->erase(id);
      continue;
    }
    if (*maxData == 0) {
      XLOG(DBG4) << "Blocked on QUIC flow control for stream " << streamId;
      priorityQueue_->erase(id);
      quicSocket_->notifyPendingWriteOnStream(streamId, this);
      continue;
    }
    auto streamData = sm_.dequeue(*wh, *maxData);
    if (streamData.data || streamData.fin) {
      auto res = quicSocket_->writeChain(streamId,
                                         std::move(streamData.data),
                                         streamData.fin,
                                         streamData.deliveryCallback);
      if (res.hasError()) {
        XLOG(ERR) << "Failed to write to stream " << streamId;
        wh->resetStream(WebTransport::kInternalError);
      }
    }
  }
}

// -- WtStreamManager::IngressCallback overrides --
void QuicWtSession::onNewPeerStream(uint64_t /*streamId*/) noexcept {
}

// -- StreamWriteCallback overrides --
void QuicWtSession::onStreamWriteReady(quic::StreamId streamId,
                                       uint64_t /*maxToSend*/) noexcept {
  auto* wh = sm_.getBidiHandle(streamId).writeHandle;
  if (wh) {
    priorityQueue_->insertOrUpdate(
        quic::PriorityQueue::Identifier::fromStreamID(wh->getID()),
        wh->getPriority());
    eventsAvailable();
  }
}

void QuicWtSession::onStreamWriteError(quic::StreamId streamId,
                                       QuicError error) noexcept {
  XLOG(ERR) << "Write error on stream " << streamId << ": " << error;
  auto* wh = sm_.getBidiHandle(streamId).writeHandle;
  if (wh) {
    wh->resetStream(WebTransport::kInternalError);
  }
}

folly::Expected<folly::Unit, WebTransport::ErrorCode>
QuicWtSession::closeSessionImpl(folly::Optional<uint32_t> error) {
  XCHECK(quicSocket_);
  sm_.shutdown({.err = error.value_or(0), .msg = "closeSession"});
  quicSocket_->close(QuicError(quic::ApplicationErrorCode(error.value_or(0))));
  quicSocket_->setConnectionCallback(nullptr);
  quicSocket_->setDatagramCallback(nullptr);
  return folly::unit;
}

void QuicWtSession::onConnectionEndImpl(
    const folly::Optional<QuicError>& error) {
  XCHECK(quicSocket_);
  XCHECK(wtHandler_);
  folly::Optional<uint32_t> errCodeOpt;
  if (error && error->code.asApplicationErrorCode()) {
    errCodeOpt = *error->code.asApplicationErrorCode();
  }
  sm_.shutdown({.err = errCodeOpt.value_or(0), .msg = ""});
  quicSocket_->setConnectionCallback(nullptr);
  quicSocket_->setDatagramCallback(nullptr);
  wtHandler_->onSessionEnd(errCodeOpt);
}

void QuicWtSession::maybePauseIngress(
    detail::WtStreamManager::WtReadHandle& handle) noexcept {
  XCHECK(quicSocket_);
  const auto id = handle.getID();
  if (sm_.bufferedBytes(handle) >= kMaxWtIngressBuf) {
    XLOG(DBG4) << "Pausing ingress for stream " << id;
    auto res = quicSocket_->pauseRead(id);
    if (res.hasError()) {
      XLOG(ERR) << "Failed to pause read for stream " << id;
      return;
    }
  }
}

void QuicWtSession::maybeResumeIngress(
    detail::WtStreamManager::WtReadHandle& handle) noexcept {
  XCHECK(quicSocket_);
  const auto id = handle.getID();
  if (sm_.bufferedBytes(handle) < kMaxWtIngressBuf) {
    XLOG(DBG4) << "Resuming ingress for stream " << id;
    auto res = quicSocket_->resumeRead(id);
    if (res.hasError()) {
      XLOG(ERR) << "Failed to resume read for stream " << id;
    }
  }
}

} // namespace proxygen
