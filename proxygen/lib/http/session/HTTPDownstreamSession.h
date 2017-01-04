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

#include <proxygen/lib/http/session/HTTPSession.h>
#include <proxygen/lib/utils/WheelTimerInstance.h>

namespace proxygen {

class HTTPSessionStats;
class HTTPDownstreamSession final: public HTTPSession {
 public:
  /**
   * @param sock       An open socket on which any applicable TLS handshaking
   *                     has been completed already.
   * @param localAddr  Address and port of the local end of the socket.
   * @param peerAddr   Address and port of the remote end of the socket.
   * @param codec      A codec with which to parse/generate messages in
   *                     whatever HTTP-like wire format this session needs.
   */
  HTTPDownstreamSession(
      const WheelTimerInstance& timeout,
      folly::AsyncTransportWrapper::UniquePtr&& sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      HTTPSessionController* controller,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback = nullptr):
    HTTPSession(timeout, std::move(sock), localAddr, peerAddr,
                CHECK_NOTNULL(controller), std::move(codec), tinfo,
                infoCallback) {
      CHECK_EQ(codec_->getTransportDirection(), TransportDirection::DOWNSTREAM);
  }

  // allows using HTTPDownstreamSession with HHWheelTimer when it is not shared
  HTTPDownstreamSession(
      folly::HHWheelTimer* timer,
      folly::AsyncTransportWrapper::UniquePtr&& sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      HTTPSessionController* controller,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback = nullptr):
    HTTPDownstreamSession(WheelTimerInstance(timer), std::move(sock), localAddr,
        peerAddr,CHECK_NOTNULL(controller), std::move(codec), tinfo,
        infoCallback) {
  }

  void startNow() override;

 private:
  ~HTTPDownstreamSession() override;

  /**
   * Called by onHeadersComplete().
   */
  void setupOnHeadersComplete(HTTPTransaction* txn, HTTPMessage* msg) override;

  /**
   * Called by processParseError() in the downstream case. This function ensures
   * that a handler is set for the transaction.
   */
  HTTPTransaction::Handler* getParseErrorHandler(
    HTTPTransaction* txn, const HTTPException& error) override;

  /**
   * Called by transactionTimeout() in the downstream case. This function
   * ensures that a handler is set for the transaction.
   */
  HTTPTransaction::Handler* getTransactionTimeoutHandler(
    HTTPTransaction* txn) override;

  /**
   * Invoked when headers have been sent.
   */
  void onHeadersSent(const HTTPMessage& headers,
                     bool codecWasReusable) override;

  bool allTransactionsStarted() const override;

  bool onNativeProtocolUpgrade(
    HTTPCodec::StreamID streamID, CodecProtocol protocol,
    const std::string& protocolString,
    HTTPMessage& msg) override;
};

} // proxygen
