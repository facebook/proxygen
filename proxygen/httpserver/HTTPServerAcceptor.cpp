/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/HTTPServerAcceptor.h>

#include <proxygen/httpserver/RequestHandlerAdaptor.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <proxygen/lib/http/session/HTTPDownstreamSession.h>

using folly::SocketAddress;

namespace proxygen {

AcceptorConfiguration HTTPServerAcceptor::makeConfig(
    const HTTPServer::IPConfig& ipConfig,
    const HTTPServerOptions& opts) {

  AcceptorConfiguration conf;
  conf.bindAddress = ipConfig.address;
  conf.connectionIdleTimeout = opts.idleTimeout;
  conf.transactionIdleTimeout = opts.idleTimeout;
  conf.initialReceiveWindow = opts.initialReceiveWindow;
  conf.receiveStreamWindowSize = opts.receiveStreamWindowSize;
  conf.receiveSessionWindowSize = opts.receiveSessionWindowSize;
  conf.acceptBacklog = opts.listenBacklog;

  if (ipConfig.protocol == HTTPServer::Protocol::SPDY) {
    conf.plaintextProtocol = "spdy/3.1";
  } else if (ipConfig.protocol == HTTPServer::Protocol::HTTP2) {
    conf.plaintextProtocol = http2::kProtocolCleartextString;
  } else if (opts.h2cEnabled) {
    conf.allowedPlaintextUpgradeProtocols = { http2::kProtocolCleartextString };
  }

  conf.sslContextConfigs = ipConfig.sslConfigs;
  conf.allowInsecureConnectionsOnSecureServer =
      ipConfig.allowInsecureConnectionsOnSecureServer;
  conf.enableTCPFastOpen = ipConfig.enableTCPFastOpen;
  conf.fastOpenQueueSize = ipConfig.fastOpenQueueSize;
  if (ipConfig.ticketSeeds) {
    conf.initialTicketSeeds = *ipConfig.ticketSeeds;
  }
  return conf;
}

std::unique_ptr<HTTPServerAcceptor> HTTPServerAcceptor::make(
  const AcceptorConfiguration& conf,
  const HTTPServerOptions& opts,
  const std::shared_ptr<HTTPCodecFactory>& codecFactory) {
  // Create a copy of the filter chain in reverse order since we need to create
  // Handlers in that order.
  std::vector<RequestHandlerFactory*> handlerFactories;
  for (auto& f: opts.handlerFactories) {
    handlerFactories.push_back(f.get());
  }
  std::reverse(handlerFactories.begin(), handlerFactories.end());

  return std::unique_ptr<HTTPServerAcceptor>(
      new HTTPServerAcceptor(conf, codecFactory, handlerFactories));
}

HTTPServerAcceptor::HTTPServerAcceptor(
    const AcceptorConfiguration& conf,
    const std::shared_ptr<HTTPCodecFactory>& codecFactory,
    std::vector<RequestHandlerFactory*> handlerFactories)
    : HTTPSessionAcceptor(conf, codecFactory),
      handlerFactories_(handlerFactories) {}

void HTTPServerAcceptor::setCompletionCallback(std::function<void()> f) {
  completionCallback_ = f;
}

HTTPServerAcceptor::~HTTPServerAcceptor() {
}

HTTPTransactionHandler* HTTPServerAcceptor::newHandler(
    HTTPTransaction& txn,
    HTTPMessage* msg) noexcept {

  SocketAddress clientAddr, vipAddr;
  txn.getPeerAddress(clientAddr);
  txn.getLocalAddress(vipAddr);
  msg->setClientAddress(clientAddr);
  msg->setDstAddress(vipAddr);

  // Create filters chain
  RequestHandler* h = nullptr;
  for (auto& factory: handlerFactories_) {
    h = factory->onRequest(h, msg);
  }

  return new RequestHandlerAdaptor(h);
}

void HTTPServerAcceptor::onConnectionsDrained() {
  if (completionCallback_) {
    completionCallback_();
  }
}

}
