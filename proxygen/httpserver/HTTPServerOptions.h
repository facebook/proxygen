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

#include <folly/io/async/AsyncServerSocket.h>
#include <folly/SocketAddress.h>
#include <proxygen/httpserver/Filters.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <signal.h>

namespace proxygen {

/**
 * Configuration options for HTTPServer
 *
 * XXX: Provide a helper that can convert thrift/json to this config
 *      directly. We keep this object type-safe.
 */
class HTTPServerOptions {
 public:
  /**
   * Number of threads to start to handle requests. Note that this excludes
   * the thread you call `HTTPServer.start()` in.
   *
   * XXX: Put some perf numbers to help user decide how many threads to
   *      create.
   * XXX: Maybe support not creating any more worker threads and doing all
   *      the work in same thread when `threads == 0`.
   */
  size_t threads = 1;

  /**
   * Chain of RequestHandlerFactory that are used to create RequestHandler
   * which handles requests.
   *
   * You can do something like -
   *
   * handlerFactories = RequestHandlerChain()
   *    .addThen<StatsFilter>()
   *    .addThen<TraceFilter>()
   *    .addThen<AccessLogFilter>()
   *    .addThen<AppSpecificHandler>()
   *    .build();
   */
  std::vector<std::unique_ptr<RequestHandlerFactory>> handlerFactories;

  /**
   * This idle timeout serves two purposes -
   *
   * 1. How long to keep idle connections around before throwing them away.
   *
   * 2. If it takes client more than this time to send request, we fail the
   *    request.
   *
   * XXX: Maybe have separate time limit for both?
   */
  std::chrono::milliseconds idleTimeout{60000};

  /**
   * TCP server socket backlog to start with.
   */
  uint32_t listenBacklog{1024};

  /**
   * Enable cleartext upgrades to HTTP/2
   */
  bool h2cEnabled{false};

  /**
   * Signals on which to shutdown the server. Mostly you will want
   * {SIGINT, SIGTERM}. Note, if you have multiple deamons running or you want
   * to have a separate signal handler, leave this empty and handle signals
   * yourself.
   */
  std::vector<int> shutdownOn{};

  /**
   * Set to true if you want to support CONNECT request. Most likely you
   * don't want that.
   */
  bool supportsConnect{false};

  /**
   * Flow control configuration for the acceptor
   */
  size_t initialReceiveWindow{65536};
  size_t receiveStreamWindowSize{65536};
  size_t receiveSessionWindowSize{65536};

  /**
   * Set to true to enable gzip content compression. Currently false for
   * backwards compatibility.
   */
  bool enableContentCompression{false};

  /**
   * Requests smaller than the specified number of bytes will not be compressed
   */
  uint64_t contentCompressionMinimumSize{1000};

  /**
   * Zlib compression level, valid values are -1(Default) to 9(Slower).
   * 4 or 6 are a good balance between compression level and cpu usage.
   */
  int contentCompressionLevel{-1};

  /**
   * Content types to compress, all entries as lowercase
   */
  std::set<std::string> contentCompressionTypes = {
    "application/javascript",
    "application/json",
    "application/x-javascript",
    "application/xhtml+xml",
    "application/xml",
    "application/xml+rss",
    "text/css",
    "text/html",
    "text/javascript",
    "text/plain",
    "text/xml",
  };

  /**
   * This holds sockets already bound to addresses that the server
   * will listen on and will be empty once the server starts.
   */
  std::vector<folly::AsyncServerSocket::UniquePtr> preboundSockets_;

  /**
   * Bind to existing file descriptor(s).
   * AsyncServerSocket can handle multiple fds and they can be provided
   * as a vector here.
   */
  void useExistingSocket(folly::AsyncServerSocket::UniquePtr socket) {
    preboundSockets_.push_back(std::move(socket));
  }

  void useExistingSocket(int socketFd) {
    useExistingSockets({socketFd});
  }

  void useExistingSockets(const std::vector<int>& socketFds) {
    folly::AsyncServerSocket::UniquePtr socket(new folly::AsyncServerSocket);

    socket->useExistingSockets(socketFds);
    useExistingSocket(std::move(socket));
  }
};
}
