/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/samples/hq/SampleHandlers.h>
#include <string>

namespace quic { namespace samples {

class WaitReleaseHandler;

std::unordered_map<uint, WaitReleaseHandler*>&
WaitReleaseHandler::getWaitingHandlers() {
  static std::unordered_map<uint, WaitReleaseHandler*> waitingHandlers;
  return waitingHandlers;
}

std::mutex& WaitReleaseHandler::getMutex() {
  static std::mutex mutex;
  return mutex;
}

void WaitReleaseHandler::onHeadersComplete(
    std::unique_ptr<proxygen::HTTPMessage> msg) noexcept {
  VLOG(10) << "WaitReleaseHandler::onHeadersComplete";
  msg->dumpMessage(2);
  path_ = msg->getPath();
  auto idstr = msg->getQueryParam("id");

  if (msg->getMethod() != proxygen::HTTPMethod::GET ||
      idstr == proxygen::empty_string ||
      (path_ != "/wait" && path_ != "/release")) {
    sendErrorResponse("bad request\n");
    return;
  }

  auto id = folly::tryTo<uint>(idstr);
  if (!id.hasValue() || id.value() == 0) {
    sendErrorResponse("invalid id\n");
    return;
  }

  id_ = id.value();

  txn_->setIdleTimeout(std::chrono::milliseconds(120000));
  std::lock_guard<std::mutex> g(getMutex());
  auto& waitingHandlers = getWaitingHandlers();
  if (path_ == "/wait") {
    auto waitHandler = waitingHandlers.find(id_);
    if (waitHandler != waitingHandlers.end()) {
      sendErrorResponse("id already exists\n");
      return;
    }
    waitingHandlers.insert(std::make_pair(id_, this));
    sendOkResponse("waiting\n", false /* eom */);
  } else if (path_ == "/release") {
    auto waitHandler = waitingHandlers.find(id.value());
    if (waitHandler == waitingHandlers.end()) {
      sendErrorResponse("id does not exist\n");
      return;
    }
    waitHandler->second->release();
    waitingHandlers.erase(waitHandler);
    sendOkResponse("released\n", true /* eom */);
  }
}

void WaitReleaseHandler::maybeCleanup() {
  if (path_ == "/wait" && id_ != 0) {
    std::lock_guard<std::mutex> g(getMutex());
    auto& waitingHandlers = getWaitingHandlers();
    auto waitHandler = waitingHandlers.find(id_);
    if (waitHandler != waitingHandlers.end()) {
      waitingHandlers.erase(waitHandler);
    }
  }
}

}} // namespace quic::samples
