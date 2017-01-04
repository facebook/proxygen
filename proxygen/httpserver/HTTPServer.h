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

#include <wangle/ssl/SSLContextConfig.h>
#include <folly/io/async/EventBase.h>
#include <wangle/bootstrap/ServerBootstrap.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/lib/http/codec/HTTPCodecFactory.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <thread>

namespace proxygen {

class SignalHandler;
class HTTPServerAcceptor;

/**
 * HTTPServer based on proxygen http libraries
 */
class HTTPServer final {
 public:
  /**
   * For each IP you can specify HTTP protocol to use.  You can use plain old
   * HTTP/1.1 protocol or SPDY/3.1 for now.
   */
  enum class Protocol: uint8_t {
    HTTP,
    SPDY,
    HTTP2,
  };

  struct IPConfig {
    IPConfig(folly::SocketAddress a,
             Protocol p,
             std::shared_ptr<HTTPCodecFactory> c = nullptr)
        : address(a),
          protocol(p),
          codecFactory(c) {}

    folly::SocketAddress address;
    Protocol protocol;
    std::shared_ptr<HTTPCodecFactory> codecFactory;
    std::vector<wangle::SSLContextConfig> sslConfigs;

    /*
     * Sets the initial ticket seeds to use when starting the HTTPServer.
     * Ticket seeds are used to generate the session ticket encryption keys
     * for ticket resumption. When using session tickets, it is important
     * to change them and keep them updated, see updateTicketSeeds to keep
     * seeds up to date.
     */
    folly::Optional<wangle::TLSTicketKeySeeds> ticketSeeds;

    /*
     * Whether to allow an insecure connection on a secure port.
     * This should be used in very few cases where a HTTP server needs to
     * support insecure and secure connections.
     */
    bool allowInsecureConnectionsOnSecureServer{false};
    bool enableTCPFastOpen{false};
    /**
     * Maximum queue size of pending fast open connections.
     */
    uint32_t fastOpenQueueSize{10000};
  };

  /**
   * Create a new HTTPServer
   */
  explicit HTTPServer(HTTPServerOptions options);
  ~HTTPServer();

  /**
   * Bind server to the following addresses. Can be called from any thread.
   *
   * Throws exception on error (say port is already busy). You can try binding
   * to different set of ports. Though once it succeeds, it is a FATAL to call
   * it again.
   *
   * The list is updated in-place to contain final port server bound to if
   * ephemeral port was given. If the call fails, the list might be partially
   * updated.
   */
  void bind(std::vector<IPConfig>& addrs);

  /**
   * Start HTTPServer.
   *
   * Note this is a blocking call and the current thread will be used to listen
   * for incoming connections. Throws exception if something goes wrong (say
   * somebody else is already listening on that socket).
   *
   * `onSuccess` callback will be invoked from the event loop which shows that
   * all the setup was successfully done.
   *
   * `onError` callback will be invoked if some errors occurs while starting the
   * server instead of throwing exception.
   */
  void start(std::function<void()> onSuccess = nullptr,
             std::function<void(std::exception_ptr)> onError = nullptr);

  /**
   * Stop listening on bound ports. (Stop accepting new work).
   * It does not wait for pending work to complete.
   * You must still invoke stop() before destroying the server.
   * You do NOT need to invoke this before calling stop().
   * This can be called from any thread, and it is idempotent.
   * However, it may only be called **after** start() has called onSuccess.
   */
  void stopListening();

  /**
   * Stop HTTPServer.
   *
   * Can be called from any thread, but only after start() has called
   * onSuccess.  Server will stop listening for new connections and will
   * wait for running requests to finish.
   *
   * TODO: Separate method to do hard shutdown?
   */
  void stop();

  /**
   * Get the list of addresses server is listening on. Empty if sockets are not
   * bound yet.
   */
  std::vector<IPConfig> addresses() const {
    return addresses_;
  }

  /**
   * Get the sockets the server is currently bound to.
   */
  const std::vector<const folly::AsyncSocketBase*> getSockets() const;

  void setSessionInfoCallback(HTTPSession::InfoCallback* cb) {
    sessionInfoCb_ = cb;
  }

  /**
   * Returns a file descriptor associated with the listening socket
   */
  int getListenSocket() const;

  /**
   * Updates ticket seeds for the HTTPServer for all the VIPs.
   */
  void updateTicketSeeds(wangle::TLSTicketKeySeeds seeds);

 private:
  std::shared_ptr<HTTPServerOptions> options_;

  /**
   * Event base in which we binded server sockets.
   */
  folly::EventBase* mainEventBase_{nullptr};

  /**
   * Optional signal handlers on which we should shutdown server
   */
  std::unique_ptr<SignalHandler> signalHandler_;

  /**
   * Addresses we are listening on
   */
  std::vector<IPConfig> addresses_;
  std::vector<wangle::ServerBootstrap<wangle::DefaultPipeline>> bootstrap_;

  /**
   * Callback for session create/destruction
   */
  HTTPSession::InfoCallback* sessionInfoCb_{nullptr};
};

}
