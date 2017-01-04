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

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/lib/http/session/HTTPSessionAcceptor.h>

namespace proxygen {

class HTTPServerAcceptor final : public HTTPSessionAcceptor {
 public:
  static AcceptorConfiguration makeConfig(
    const HTTPServer::IPConfig& ipConfig,
    const HTTPServerOptions& opts);

  static std::unique_ptr<HTTPServerAcceptor> make(
    const AcceptorConfiguration& conf,
    const HTTPServerOptions& opts,
    const std::shared_ptr<HTTPCodecFactory>& codecFactory = nullptr);

  /**
   * Invokes the given method when all the connections are drained
   */
  void setCompletionCallback(std::function<void()> f);

  ~HTTPServerAcceptor() override;

 private:
  HTTPServerAcceptor(const AcceptorConfiguration& conf,
                     const std::shared_ptr<HTTPCodecFactory>& codecFactory,
                     std::vector<RequestHandlerFactory*> handlerFactories);

  // HTTPSessionAcceptor
  HTTPTransaction::Handler* newHandler(HTTPTransaction& txn,
                                       HTTPMessage* msg) noexcept override;
  void onConnectionsDrained() override;

  std::function<void()> completionCallback_;
  const std::vector<RequestHandlerFactory*> handlerFactories_{nullptr};
};

}
