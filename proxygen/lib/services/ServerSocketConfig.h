/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "proxygen/lib/ssl/SSLCacheOptions.h"
#include "proxygen/lib/ssl/SSLContextConfig.h"
#include "proxygen/lib/ssl/SSLUtil.h"
#include "proxygen/lib/ssl/TLSTicketKeySeeds.h"
#include "proxygen/lib/utils/SocketOptions.h"

#include <boost/optional.hpp>
#include <chrono>
#include <fcntl.h>
#include <folly/Random.h>
#include <folly/SocketAddress.h>
#include <folly/String.h>
#include <list>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp/transport/TSSLSocket.h>

namespace proxygen {

/**
 * Configuration for a single Acceptor.
 *
 * This configures not only accept behavior, but also some types of SSL
 * behavior that may make sense to configure on a per-VIP basis (e.g. which
 * cert(s) we use, etc).
 */
struct ServerSocketConfig {
  ServerSocketConfig() {
    // generate a single random current seed
    uint8_t seed[32];
    folly::Random::secureRandom(seed, sizeof(seed));
    initialTicketSeeds.currentSeeds.push_back(
      SSLUtil::hexlify(std::string((char *)seed, sizeof(seed))));
  }

  bool isSSL() const { return !(sslContextConfigs.empty()); }

  /**
   * Set/get the socket options to apply on all downstream connections.
   */
  void setSocketOptions(
    const apache::thrift::async::TAsyncSocket::OptionMap& opts) {
    socketOptions_ = filterIPSocketOptions(opts, bindAddress.getFamily());
  }
  apache::thrift::async::TAsyncSocket::OptionMap&
  getSocketOptions() {
    return socketOptions_;
  }
  const apache::thrift::async::TAsyncSocket::OptionMap&
  getSocketOptions() const {
    return socketOptions_;
  }

  bool hasExternalPrivateKey() const {
    for (const auto& cfg : sslContextConfigs) {
      if (!cfg.isLocalPrivateKey) {
        return true;
      }
    }
    return false;
  }

  /**
   * The name of this acceptor; used for stats/reporting purposes.
   */
  std::string name;

  /**
   * The depth of the accept queue backlog.
   */
  uint32_t acceptBacklog{1024};

  /**
   * The number of milliseconds a connection can be idle before we close it.
   */
  std::chrono::milliseconds connectionIdleTimeout{600000};

  /**
   * The address to bind to.
   */
  folly::SocketAddress bindAddress;

  /**
   * Options for controlling the SSL cache.
   */
  SSLCacheOptions sslCacheOptions{std::chrono::seconds(600), 20480, 200};

  /**
   * The initial TLS ticket seeds.
   */
  TLSTicketKeySeeds initialTicketSeeds;

  /**
   * The configs for all the SSL_CTX for use by this Acceptor.
   */
  std::vector<SSLContextConfig> sslContextConfigs;

  /**
   * Determines if the Acceptor does strict checking when loading the SSL
   * contexts.
   */
  bool strictSSL{true};

  /**
   * Maximum number of concurrent pending SSL handshakes
   */
  uint32_t maxConcurrentSSLHandshakes{30720};

 private:
  apache::thrift::async::TAsyncSocket::OptionMap socketOptions_;
};

} // proxygen
