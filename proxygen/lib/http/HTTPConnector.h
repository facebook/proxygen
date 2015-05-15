/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/wangle/acceptor/TransportInfo.h>
#include <folly/io/async/SSLContext.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <proxygen/lib/utils/Time.h>
#include <folly/io/async/AsyncSocket.h>

namespace proxygen {

class HTTPUpstreamSession;

/**
 * This class establishes new connections to HTTP or HTTPS servers. It
 * can be reused, even to connect to different addresses, but it can only
 * service setting up one connection at a time.
 */
class HTTPConnector:
      private folly::AsyncSocket::ConnectCallback {
 public:
  /**
   * This class defines the pure virtual interface on which to receive the
   * result on.
   */
  class Callback {
   public:
    virtual ~Callback() {}
    virtual void connectSuccess(HTTPUpstreamSession* session) = 0;
    virtual void connectError(
      const folly::AsyncSocketException& ex) = 0;
  };

  /**
   * Construct a HTTPConnector. The constructor arguments are those
   * parameters HTTPConnector needs to keep a copy of through the
   * connection process.
   *
   * @param callback The interface on which to receive the result.
   *                 Whatever object is passed here MUST outlive this
   *                 connector and MUST NOT be null.
   * @param timeoutSet The timeout set to be used for the transactions
   *                   that are opened on the session.
   * @param plaintextProto An optional protocol string to specify the
   *                       next protocol to use for unsecure connections.
   *                       If omitted, http/1.1 will be assumed.
   * @param forceHTTP1xCodecTo11 If true and this connector creates
   *                             a session using an HTTP1xCodec, that codec will
   *                             only serialize messages as HTTP/1.1.
   */
  HTTPConnector(Callback* callback, AsyncTimeoutSet* timeoutSet);

  /**
   * Clients may delete the connector at any time to cancel it. No
   * callbacks will be received.
   */
  ~HTTPConnector() override;

  /**
   * Reset the object so that it can begin a new connection. No callbacks
   * will be invoked as a result of executing this function. After this
   * function returns, isBusy() will return false.
   */
  void reset();

  /**
   * Sets the plain text protocol to use after the connection
   * is established.
   */
  void setPlaintextProtocol(const std::string& plaintextProto);

  /**
   * Overrides the HTTP version to always use the latest and greatest
   * version we support.
   */
  void setHTTPVersionOverride(bool enabled);

  /**
   * Begin the process of getting a plaintext connection to the server
   * specified by 'connectAddr'. This function immediately starts async
   * work and may invoke functions on Callback immediately.
   *
   * @param eventBase The event base to put events on.
   * @param connectAddr The address to connect to.
   * @param timeoutMs Optional. If this value is greater than zero, then a
   *                  connect error will be given if no connection is
   *                  established within this amount of time.
   * @param socketOptions Optional socket options to set on the connection.
   * @param bindAddr Optional address to bind to locally.
   */
  void connect(
    folly::EventBase* eventBase,
    const folly::SocketAddress& connectAddr,
    std::chrono::milliseconds timeoutMs = std::chrono::milliseconds(0),
    const folly::AsyncSocket::OptionMap& socketOptions =
      folly::AsyncSocket::emptyOptionMap,
    const folly::SocketAddress& bindAddr =
      folly::AsyncSocket::anyAddress());

  /**
   * Begin the process of getting a secure connection to the server
   * specified by 'connectAddr'. This function immediately starts async
   * work and may invoke functions on Callback immediately.
   *
   * @param eventBase The event base to put events on.
   * @param connectAddr The address to connect to.
   * @param ctx SSL context to use. Must not be null.
   * @param session Optional ssl session to use.
   * @param timeoutMs Optional. If this value is greater than zero, then a
   *                  connect error will be given if no connection is
   *                  established within this amount of time.
   * @param socketOptions Optional socket options to set on the connection.
   * @param bindAddr Optional address to bind to locally.
   */
  void connectSSL(
    folly::EventBase* eventBase,
    const folly::SocketAddress& connectAddr,
    const std::shared_ptr<folly::SSLContext>& ctx,
    SSL_SESSION* session = nullptr,
    std::chrono::milliseconds timeoutMs = std::chrono::milliseconds(0),
    const folly::AsyncSocket::OptionMap& socketOptions =
      folly::AsyncSocket::emptyOptionMap,
    const folly::SocketAddress& bindAddr =
      folly::AsyncSocket::anyAddress());

  /**
   * @returns the number of milliseconds since connecting began, or
   * zero if connecting hasn't started yet.
   */
  std::chrono::milliseconds timeElapsed();

  /**
   * @returns true iff this connector is busy setting up a connection. If
   * this is false, it is safe to call connect() or connectSSL() on it again.
   */
  bool isBusy() const { return socket_.get(); }

 private:
  void connectSuccess() noexcept override;
  void connectErr(const folly::AsyncSocketException& ex)
    noexcept override;

  Callback* cb_;
  AsyncTimeoutSet* timeoutSet_;
  folly::AsyncSocket::UniquePtr socket_;
  folly::TransportInfo transportInfo_;
  std::string plaintextProtocol_;
  TimePoint connectStart_;
  bool forceHTTP1xCodecTo1_1_{false};
};

}
