/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "WebSocketClient.h"

#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/utils/LogShim.h>
#include <utility>

using namespace folly;
using namespace proxygen;
using namespace std;

namespace websocketclient {

WebSocketClient::WebSocketClient(EventBase* evb, proxygen::URL url)
    : evb_(evb), url_(std::move(url)) {
}

void WebSocketClient::connectSuccess(HTTPUpstreamSession* session) {
  sendRequest(session->newTransaction(this));
  session->closeWhenIdle();
}

void WebSocketClient::setupRequest() {
  request_.setMethod(HTTPMethod::GET);
  request_.setHTTPVersion(1, 1);
  request_.setURL(url_.makeRelativeURL());
  request_.getHeaders().add(HTTP_HEADER_USER_AGENT,
                            "proxygen_websocket_client");
  request_.getHeaders().add(HTTP_HEADER_HOST, url_.getHostAndPort());
  request_.getHeaders().add("Accept", "*/*");
  request_.setEgressWebsocketUpgrade();
}

void WebSocketClient::sendRequest(HTTPTransaction* txn) {
  PRX_VLOG(4) << fmt::format("Connecting to {}", url_.getUrl());
  txn_ = txn;
  setupRequest();
  txn_->sendHeaders(request_);
}

void WebSocketClient::connectError(const folly::AsyncSocketException& ex) {
  PRX_LOG(ERROR) << "Failed to connect to " << url_.getHostAndPort() << ":"
                 << ex.what();
}

void WebSocketClient::setTransaction(HTTPTransaction*) noexcept {
}

void WebSocketClient::detachTransaction() noexcept {
}

void WebSocketClient::onHeadersComplete(unique_ptr<HTTPMessage> msg) noexcept {
  response_ = std::move(msg);
}

void WebSocketClient::onBody(std::unique_ptr<folly::IOBuf> chain) noexcept {
  PRX_LOG(INFO) << "got server reply: " << chain->moveToFbString();
  // Close the connection.
  txn_->sendEOM();
}

void WebSocketClient::onTrailers(std::unique_ptr<HTTPHeaders>) noexcept {
  PRX_CHECK(false) << "unexpected trailers";
}

void WebSocketClient::onEOM() noexcept {
  PRX_LOG(INFO) << "connection closed by server";
}

void WebSocketClient::onUpgrade(UpgradeProtocol) noexcept {
  PRX_LOG(INFO) << "websocket connect successful; sending data";
  auto data = folly::IOBuf::copyBuffer("websocket | framed | stream | data");
  txn_->sendBody(std::move(data));
}

void WebSocketClient::onError(const HTTPException& error) noexcept {
  PRX_LOG(ERROR) << "An error occurred: " << error.what();
}

} // namespace websocketclient
