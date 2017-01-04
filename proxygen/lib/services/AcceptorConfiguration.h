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

#include <chrono>
#include <fcntl.h>
#include <folly/String.h>
#include <wangle/acceptor/ServerSocketConfig.h>
#include <list>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <folly/io/async/AsyncSocket.h>
#include <zlib.h>

namespace proxygen {

/**
 * Configuration for a single Acceptor.
 *
 * This configures not only accept behavior, but also some types of SSL
 * behavior that may make sense to configure on a per-VIP basis (e.g. which
 * cert(s) we use, etc).
 */
struct AcceptorConfiguration : public wangle::ServerSocketConfig {
  /**
   * Determines if the VIP should accept traffic from only internal or
   * external clients. Internal VIPs have different behavior
   * (e.g. Via headers, etc).
   */
  bool internal{false};

  /**
  * Determines if connection should respect HTTP2 priorities
  **/
  bool HTTP2PrioritiesEnabled{true};

  /**
   * The number of milliseconds a transaction can be idle before we close it.
   */
  std::chrono::milliseconds transactionIdleTimeout{600000};

  /**
   * The compression level to use for SPDY headers with responses from
   * this Acceptor.
   */
  int spdyCompressionLevel{Z_NO_COMPRESSION};

  /**
   * The name of the protocol to use on non-TLS connections.
   */
  std::string plaintextProtocol;

  /**
   * Comma separated string of protocols that can be upgraded to from HTTP/1.1
   */
  std::list<std::string> allowedPlaintextUpgradeProtocols;

  /**
   * The maximum number of transactions the remote could initiate
   * per connection on protocols that allow multiplexing.
   */
  uint32_t maxConcurrentIncomingStreams{0};

  /**
   * Flow control parameters.
   *
   *  initialReceiveWindow     = amount to advertise to peer via SETTINGS
   *  receiveStreamWindowSize  = amount to increase per-stream window via
   *                             WINDOW_UPDATE
   *  receiveSessionWindowSize = amount to increase per-session window via
   *                             WINDOW_UPDATE
   *                             This also controls the size of the per-session
   *                             read buffer.
   */
  size_t initialReceiveWindow{65536};
  size_t receiveStreamWindowSize{65536};
  size_t receiveSessionWindowSize{65536};

  /**
   * These parameters control how many bytes HTTPSession's will buffer in user
   * space before applying backpressure to handlers.  -1 means use the
   * built-in HTTPSession default (64kb)
   */
  int64_t writeBufferLimit{-1};
};

} // proxygen
