/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "EchoHandler.h"

#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "EchoStats.h"

using namespace proxygen;

namespace EchoService {

EchoHandler::EchoHandler(EchoStats* stats): stats_(stats) {
}

void EchoHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
  stats_->recordRequest();
  if(WebSocket::isWebSocketRequest(*headers))
    websocket_ = std::move(WebSocket::acceptWebSocket(downstream_, *headers, std::bind(&EchoHandler::onWebSocketFrame, this, std::placeholders::_1)));

}

void EchoHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if(websocket_) {
    websocket_->processData(std::move(body));
  }
  else {
    if (body_) {
      body_->prependChain(std::move(body));
    } else {
      body_ = std::move(body);
    }
  }
}

void EchoHandler::onEOM() noexcept {
  if(websocket_)
    return;
  ResponseBuilder(downstream_)
    .status(200, "OK")
    .header("Request-Number",
            folly::to<std::string>(stats_->getRequestCount()))
    .body(std::move(body_))
    .sendWithEOM();
}

void EchoHandler::onWebSocketFrame(std::unique_ptr<proxygen::WebSocketFrame> frame) {
  websocket_->sendFrame(frame->frameType, std::move(frame->payload), frame->endOfMessage);
}

void EchoHandler::onUpgrade(UpgradeProtocol protocol) noexcept {

}

void EchoHandler::requestComplete() noexcept {
  delete this;
}

void EchoHandler::onError(ProxygenError err) noexcept {
  delete this;
}

}
