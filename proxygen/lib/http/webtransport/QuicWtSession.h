/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/logging/xlog.h>
#include <proxygen/lib/http/webtransport/WebTransport.h>
#include <proxygen/lib/http/webtransport/WtStreamManager.h>
#include <proxygen/lib/http/webtransport/WtUtils.h>
#include <quic/api/QuicSocket.h>
#include <quic/priority/HTTPPriorityQueue.h>

namespace proxygen {

using QuicSocket = quic::QuicSocket;
using StreamId = quic::StreamId;
using QuicError = quic::QuicError;

/**
 * This class is effectively an adaptor, it exposes a WebTransport interace over
 * a quic::QuicSocket. It's a pure virtual interface because we have two main
 * expected use cases / derived classes:
 *
 *   - exposing a direct mapping of WebTransport to quic, this implementation
 *     has a unique ownership of the QuicSocket and all of QuicSocket's stream
 *     data is directly piped into WtStreamManager.
 *
 *   - exposing a mapping of WebTransport to http/3 over quic, this
 *     implementation has a shared ownership of the QuicSocket and the backing
 *     http/3 session (HTTPQuicCoroSession/HQSession) will manage a subset (only
 *     those belonging to the original CONNECT stream) of QuicSocket's streams
 *     via QuicWtSessionBase.
 */

class QuicWtSessionBase
    : public detail::WtSessionBase
    , private quic::StreamWriteCallback {
 public:
  [[nodiscard]] quic::TransportInfo getTransportInfo() const noexcept override {
    XCHECK(quicSocket_);
    return quicSocket_->getTransportInfo();
  }

  folly::Expected<StreamWriteHandle*, ErrorCode> createUniStream() noexcept
      override;
  folly::Expected<BidiStreamHandle, ErrorCode> createBidiStream() noexcept
      override;
  folly::SemiFuture<folly::Unit> awaitUniStreamCredit() noexcept override;
  folly::SemiFuture<folly::Unit> awaitBidiStreamCredit() noexcept override;

  folly::Expected<folly::Unit, ErrorCode> sendDatagram(
      IoBufPtr datagram) noexcept override;

  [[nodiscard]] const folly::SocketAddress& getLocalAddress() const override {
    return quicSocket_->getLocalAddress();
  }

  [[nodiscard]] const folly::SocketAddress& getPeerAddress() const override {
    return quicSocket_->getPeerAddress();
  }

  folly::Expected<folly::Unit, ErrorCode> closeSession(
      folly::Optional<uint32_t> error) noexcept override;

 protected:
  QuicWtSessionBase(std::shared_ptr<QuicSocket> quicSocket,
                    std::unique_ptr<WebTransportHandler> wtHandler,
                    detail::WtStreamManager::WtConfig wtConfig);
  ~QuicWtSessionBase() override = 0;

  void onDatagram(IoBufPtr dgram) noexcept;

  struct QuicReadCallback : public quic::QuicSocket::ReadCallback {
    QuicWtSessionBase& sess;
    explicit QuicReadCallback(QuicWtSessionBase& session) : sess(session) {
    }
    void readAvailable(StreamId streamId) noexcept override;
    void readError(StreamId streamId, QuicError error) noexcept override;
  } readCb_{*this};

  struct QuicStopSendingCallback : public quic::StopSendingCallback {
    QuicWtSessionBase& sess;
    explicit QuicStopSendingCallback(QuicWtSessionBase& session)
        : sess(session) {
    }
    void onStopSending(StreamId id,
                       quic::ApplicationErrorCode ec) noexcept override;
  } stopSendingCb_{*this};

  struct StreamManagerCallback
      : public detail::WtStreamManager::ReadCallback
      , public detail::WtStreamManager::EgressCallback
      , public detail::WtStreamManager::IngressCallback {
    QuicWtSessionBase& sess;
    explicit StreamManagerCallback(QuicWtSessionBase& session) : sess(session) {
    }
    void readReady(detail::WtStreamManager::WtReadHandle& rh) noexcept override;
    void eventsAvailable() noexcept override;
    void onNewPeerStream(uint64_t streamId) noexcept override;
  } smCb_{*this};

  /**
   * attempts to acquire an ingress stream id - returns false if it fails or
   * true if successful.
   */
  bool acquireIngressStream(uint64_t id) noexcept;

  std::shared_ptr<quic::QuicSocket> quicSocket_{nullptr};
  std::unique_ptr<WebTransportHandler> wtHandler_;
  std::unique_ptr<quic::HTTPPriorityQueue> priorityQueue_;
  detail::WtStreamManager sm_;

 private:
  /**
   * Returns true iff there is bidi/uni credit w.r.t both QuicSocket &
   * WtStreamManager. If this returns true, we can safely proceed to create
   * egress streams via ::create(Uni|Bidi)Stream
   */
  bool hasEgressUniCredit() const noexcept;
  bool hasEgressBidiCredit() const noexcept;

  BidiStreamHandle createWtEgressHandle(StreamId id) noexcept;

  // -- StreamWriteCallback overrides --
  void onStreamWriteReady(StreamId id, uint64_t maxToSend) noexcept override;
  void onStreamWriteError(StreamId id, QuicError error) noexcept override;

  void maybePauseIngress(
      detail::WtStreamManager::WtReadHandle& handle) noexcept;
  void maybeResumeIngress(
      detail::WtStreamManager::WtReadHandle& handle) noexcept;
};

class QuicWtSession final : public QuicWtSessionBase {
 public:
  QuicWtSession(std::shared_ptr<QuicSocket> quicSocket,
                std::unique_ptr<WebTransportHandler> wtHandler);
  ~QuicWtSession() override;

  folly::Expected<folly::Unit, ErrorCode> closeSession(
      folly::Optional<uint32_t> error) noexcept override;

 private:
  struct QuicConnectionCallback : public QuicSocket::ConnectionCallback {
    QuicWtSession& sess;
    explicit QuicConnectionCallback(QuicWtSession& session) : sess(session) {
    }
    void onNewBidirectionalStream(StreamId id) noexcept override;
    void onNewUnidirectionalStream(StreamId id) noexcept override;
    void onConnectionEnd() noexcept override;
    void onConnectionEnd(QuicError error) noexcept override;
    void onConnectionError(QuicError code) noexcept override;
    void onBidirectionalStreamsAvailable(
        uint64_t numStreamsAvailable) noexcept override;
    void onUnidirectionalStreamsAvailable(
        uint64_t numStreamsAvailable) noexcept override;
    void onStopSending(StreamId, quic::ApplicationErrorCode) noexcept override {
      // handled by per-stream QuicSocket::addStopSending
    }
  } connCb_{*this};

  struct QuicDgramCallback : public QuicSocket::DatagramCallback {
    QuicWtSession& sess;
    explicit QuicDgramCallback(QuicWtSession& session) : sess(session) {
    }
    void onDatagramsAvailable() noexcept override;
  } dgramCb_{*this};
};

} // namespace proxygen
