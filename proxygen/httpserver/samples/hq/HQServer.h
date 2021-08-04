/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <iostream>
#include <string>

#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/HTTPTransactionHandlerAdaptor.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <proxygen/httpserver/samples/hq/SampleHandlers.h>
#include <proxygen/lib/http/session/HQDownstreamSession.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/utils/WheelTimerInstance.h>
#include <quic/congestion_control/ServerCongestionControllerFactory.h>
#include <quic/logging/FileQLogger.h>
#include <quic/server/QuicCcpThreadLauncher.h>
#include <quic/server/QuicServer.h>
#include <quic/server/QuicServerTransport.h>
#include <quic/server/QuicSharedUDPSocketFactory.h>

namespace quic { namespace samples {

using HTTPTransactionHandlerProvider =
    std::function<proxygen::HTTPTransactionHandler*(proxygen::HTTPMessage*,
                                                    const HQParams&)>;

/**
 * The Dispatcher object is responsible for spawning
 * new request handlers, based on the path.
 */
class Dispatcher {
 public:
  static proxygen::HTTPTransactionHandler* getRequestHandler(
      proxygen::HTTPMessage* /* msg */, const HQParams& /* params */);
};

/**
 * HQSessionController creates new HQSession objects
 *
 * Each HQSessionController object can create only a single session
 * object. TODO: consider changing it.
 *
 * Once the created session object finishes (and detaches), the
 * associated HQController is destroyed. There is no need to
 * explicitly keep track of these objects.
 */
class HQSessionController
    : public proxygen::HTTPSessionController
    , proxygen::HTTPSession::InfoCallback {
 public:
  using StreamData = std::pair<folly::IOBufQueue, bool>;

  explicit HQSessionController(const HQParams& /* params */,
                               const HTTPTransactionHandlerProvider&);

  ~HQSessionController() override = default;

  // Creates new HQDownstreamSession object, initialized with params_
  proxygen::HQSession* createSession();

  // Starts the newly created session. createSession must have been called.
  void startSession(std::shared_ptr<quic::QuicSocket> /* sock */);

  void onDestroy(const proxygen::HTTPSessionBase& /* session*/) override;

  proxygen::HTTPTransactionHandler* getRequestHandler(
      proxygen::HTTPTransaction& /*txn*/,
      proxygen::HTTPMessage* /* msg */) override;

  proxygen::HTTPTransactionHandler* getParseErrorHandler(
      proxygen::HTTPTransaction* /*txn*/,
      const proxygen::HTTPException& /*error*/,
      const folly::SocketAddress& /*localAddress*/) override;

  proxygen::HTTPTransactionHandler* getTransactionTimeoutHandler(
      proxygen::HTTPTransaction* /*txn*/,
      const folly::SocketAddress& /*localAddress*/) override;

  void attachSession(proxygen::HTTPSessionBase* /*session*/) override;

  // The controller instance will be destroyed after this call.
  void detachSession(const proxygen::HTTPSessionBase* /*session*/) override;

  void onTransportReady(proxygen::HTTPSessionBase* /*session*/) override;
  void onTransportReady(const proxygen::HTTPSessionBase&) override {
  }

 private:
  // The owning session. NOTE: this must be a plain pointer to
  // avoid circular references
  proxygen::HQSession* session_{nullptr};
  // Configuration params
  const HQParams& params_;
  // Provider of HTTPTransactionHandler, owned by HQServerTransportFactory
  const HTTPTransactionHandlerProvider& httpTransactionHandlerProvider_;

  void sendKnobFrame(const folly::StringPiece str);
};

class HQServerTransportFactory : public quic::QuicServerTransportFactory {
 public:
  explicit HQServerTransportFactory(
      const HQParams& params,
      const HTTPTransactionHandlerProvider& httpTransactionHandlerProvider);
  ~HQServerTransportFactory() override = default;

  // Creates new quic server transport
  quic::QuicServerTransport::Ptr make(
      folly::EventBase* evb,
      std::unique_ptr<folly::AsyncUDPSocket> socket,
      const folly::SocketAddress& /* peerAddr */,
      quic::QuicVersion quicVersion,
      std::shared_ptr<const fizz::server::FizzServerContext> ctx) noexcept
      override;

 private:
  // Configuration params
  const HQParams& params_;
  // Provider of HTTPTransactionHandler
  HTTPTransactionHandlerProvider httpTransactionHandlerProvider_;
};

class HQServer {
 public:
  explicit HQServer(
      const HQParams& params,
      HTTPTransactionHandlerProvider httpTransactionHandlerProvider);

  //  TODO is this needed? can it be invoked in constructor?
  //  TODO is the params argument needed? why not params_ field?
  void setTlsSettings(const HQParams& params);

  // Starts the QUIC transport in background thread
  void start();

  // Starts the HTTP server handling loop on the current EVB
  void run();

  // Returns the listening address of the server
  // NOTE: can block until the server has started
  const folly::SocketAddress getAddress() const;

  // Stops both the QUIC transport AND the HTTP server handling loop
  void stop();

  // Sets/unsets "reject connections" flag on the QUIC server
  void rejectNewConnections(bool reject);

 private:
  const HQParams& params_;
  folly::EventBase eventbase_;
  std::shared_ptr<quic::QuicServer> server_;
  folly::Baton<> cv_;
  QuicCcpThreadLauncher quicCcpThreadLauncher_;
};

class H2Server {
  class SampleHandlerFactory : public proxygen::RequestHandlerFactory {
   public:
    explicit SampleHandlerFactory(
        const HQParams& params,
        HTTPTransactionHandlerProvider httpTransactionHandlerProvider);

    void onServerStart(folly::EventBase* /*evb*/) noexcept override;

    void onServerStop() noexcept override;

    proxygen::RequestHandler* onRequest(
        proxygen::RequestHandler* /* handler */,
        proxygen::HTTPMessage* /* msg */) noexcept override;

   private:
    const HQParams& params_;
    HTTPTransactionHandlerProvider httpTransactionHandlerProvider_;
  }; // SampleHandlerFactory

 public:
  static std::unique_ptr<proxygen::HTTPServerOptions> createServerOptions(
      const HQParams& /* params */,
      HTTPTransactionHandlerProvider httpTransactionHandlerProvider);
  using AcceptorConfig = std::vector<proxygen::HTTPServer::IPConfig>;
  static std::unique_ptr<AcceptorConfig> createServerAcceptorConfig(
      const HQParams& /* params */);
  // Starts H2 server in a background thread
  static std::thread run(
      const HQParams& params,
      HTTPTransactionHandlerProvider httpTransactionHandlerProvider);
};

void startServer(const HQParams& params);

}} // namespace quic::samples
