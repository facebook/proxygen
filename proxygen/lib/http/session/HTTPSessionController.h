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

#include <glog/logging.h>
#include <chrono>

namespace folly {
class SocketAddress;
}

namespace proxygen {

class HTTPException;
class HTTPMessage;
class HTTPSession;
class HTTPTransaction;
class HTTPTransactionHandler;

class HTTPSessionController {
 public:
  virtual ~HTTPSessionController() {}

  /**
   * Will be invoked whenever HTTPSession successfully parses a
   * request
   *
   * The controller creates a Handler for a new transaction.  The
   * transaction and HTTP message (request) are passed so the
   * implementation can construct different handlers based on these.
   * The transaction will be explicitly set on the handler later via
   * setTransaction.  The request message will be passed in
   * onHeadersComplete.
   */
  virtual HTTPTransactionHandler* getRequestHandler(
    HTTPTransaction& txn, HTTPMessage* msg) = 0;

  /**
   * Will be invoked when HTTPSession is unable to parse a new request
   * on the connection because of bad input.
   *
   * error contains specific information about what went wrong
   */
  virtual HTTPTransactionHandler* getParseErrorHandler(
    HTTPTransaction* txn,
    const HTTPException& error,
    const folly::SocketAddress& localAddress) = 0;

  /**
   * Will be invoked when HTTPSession times out parsing a new request.
   */
  virtual HTTPTransactionHandler* getTransactionTimeoutHandler(
    HTTPTransaction* txn,
    const folly::SocketAddress& localAddress) = 0;

  /**
   * Inform the controller it is associated with this particular session.
   */
  virtual void attachSession(HTTPSession* session) = 0;

  /**
   * Informed at the end when the given HTTPSession is going away.
   */
  virtual void detachSession(const HTTPSession* session) = 0;

  /**
   * Inform the controller that the session's codec changed
   */
  virtual void onSessionCodecChange(HTTPSession* session) {}

  virtual std::chrono::milliseconds getGracefulShutdownTimeout() const {
    return std::chrono::milliseconds(0);
  }
};


class HTTPUpstreamSessionController : public HTTPSessionController {
  HTTPTransactionHandler* getRequestHandler(
    HTTPTransaction& txn, HTTPMessage* msg) override final {
    LOG(FATAL) << "Unreachable";
    return nullptr;
  }

  /**
   * Will be invoked when HTTPSession is unable to parse a new request
   * on the connection because of bad input.
   *
   * error contains specific information about what went wrong
   */
  HTTPTransactionHandler* getParseErrorHandler(
    HTTPTransaction* txn,
    const HTTPException& error,
    const folly::SocketAddress& localAddress) override final {
    LOG(FATAL) << "Unreachable";
    return nullptr;
  }

  /**
   * Will be invoked when HTTPSession times out parsing a new request.
   */
  HTTPTransactionHandler* getTransactionTimeoutHandler(
    HTTPTransaction* txn,
    const folly::SocketAddress& localAddress) override final {
    LOG(FATAL) << "Unreachable";
    return nullptr;
  }
};

} // proxygen
