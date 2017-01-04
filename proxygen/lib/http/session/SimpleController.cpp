/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/SimpleController.h>

#include <proxygen/lib/http/session/CodecErrorResponseHandler.h>
#include <proxygen/lib/http/session/HTTPDirectResponseHandler.h>
#include <proxygen/lib/http/session/HTTPSessionAcceptor.h>

namespace proxygen {

SimpleController::SimpleController(HTTPSessionAcceptor* acceptor)
    : acceptor_(acceptor) {
}

HTTPTransactionHandler* SimpleController::getRequestHandler(
  HTTPTransaction& txn, HTTPMessage* msg) {
  return acceptor_->newHandler(txn, msg);
}

HTTPTransactionHandler* SimpleController::getParseErrorHandler(
    HTTPTransaction* txn,
    const HTTPException& error,
    const folly::SocketAddress& localAddress) {

  if (error.hasCodecStatusCode()) {
    return new CodecErrorResponseHandler(error.getCodecStatusCode());
  }

  auto errorPage = acceptor_->getErrorPage(localAddress);
  return createErrorHandler(error.hasHttpStatusCode() ?
                            error.getHttpStatusCode() : 400,
                            "Bad Request", errorPage);
}

HTTPTransactionHandler* SimpleController::getTransactionTimeoutHandler(
    HTTPTransaction* txn,
    const folly::SocketAddress& localAddress) {

  auto errorPage = acceptor_->getErrorPage(localAddress);
  return createErrorHandler(408, "Client timeout", errorPage);
}

void SimpleController::attachSession(HTTPSession* sess) {
}

void SimpleController::detachSession(const HTTPSession* sess) {
}

HTTPTransactionHandler* SimpleController::createErrorHandler(
    uint32_t statusCode,
    const std::string& statusMessage,
    const HTTPErrorPage* errorPage) {

  return new HTTPDirectResponseHandler(statusCode,
                                       statusMessage,
                                       errorPage);
}

std::chrono::milliseconds SimpleController::getGracefulShutdownTimeout() const {
  return acceptor_->getGracefulShutdownTimeout();
}
}
