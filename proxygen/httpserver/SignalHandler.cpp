/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/SignalHandler.h>

#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/HTTPServer.h>

using folly::EventBaseManager;

namespace proxygen {

SignalHandler::SignalHandler(HTTPServer* server)
    : folly::AsyncSignalHandler(EventBaseManager::get()->getEventBase()),
      server_(server) {
}

void SignalHandler::install(const std::vector<int>& signals) {
  for (const int& signal: signals) {
    registerSignalHandler(signal);
  }
}

void SignalHandler::signalReceived(int signum) noexcept {
  server_->stop();
}

}
