/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <optional>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <string>

namespace proxygen {

class HTTPErrorPage;
class HTTPSessionAcceptor;

/**
 * This simple controller provides some basic default behaviors. When
 * errors occur, it will install an appropriate handler. Otherwise, it
 * will install the acceptor's default handler.
 */
class SimpleController : public HTTPSessionController {
 public:
  // What a parse error is answered with when the exception names no status
  // code of its own.
  static constexpr uint16_t kDefaultParseErrorStatusCode{400};

  explicit SimpleController(HTTPSessionAcceptor* acceptor);

  /**
   * Will be invoked whenever HTTPSession successfully parses a
   * request
   */
  HTTPTransactionHandler* getRequestHandler(HTTPTransaction& txn,
                                            HTTPMessage* msg) override;

  /**
   * Will be invoked when HTTPSession is unable to parse a new request
   * on the connection because of bad input.
   *
   * error contains specific information about what went wrong
   */
  HTTPTransactionHandler* getParseErrorHandler(
      HTTPTransaction* txn,
      const HTTPException& error,
      const folly::SocketAddress& localAddress) override;

  /**
   * The status code getParseErrorHandler answers `error` with, or none when it
   * resets the stream instead and so sends no status code at all.
   *
   * Whether a rejected request is answered with a response or a reset is not
   * something the exception states directly -- it follows from which of the
   * error codes it carries -- so anything that needs to report what the client
   * received should ask here rather than infer it.
   */
  static std::optional<uint16_t> getParseErrorHttpStatusCode(
      const HTTPException& error);

  /**
   * Will be invoked when HTTPSession times out parsing a new request.
   */
  HTTPTransactionHandler* getTransactionTimeoutHandler(
      HTTPTransaction* txn, const folly::SocketAddress& localAddress) override;

  void attachSession(HTTPSessionBase*) override;
  void detachSession(const HTTPSessionBase*) override;

  [[nodiscard]] std::chrono::milliseconds getGracefulShutdownTimeout()
      const override;

 protected:
  HTTPTransactionHandler* createErrorHandler(uint32_t statusCode,
                                             const std::string& statusMessage,
                                             const HTTPErrorPage* errorPage);

  HTTPSessionAcceptor* const acceptor_{nullptr};
};

} // namespace proxygen
