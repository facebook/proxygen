/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/services/RequestWorkerThread.h>

#include <folly/io/async/EventBaseManager.h>
#include <proxygen/lib/services/ServiceWorker.h>

namespace proxygen {

RequestWorkerThread::RequestWorkerThread(
  FinishCallback& callback, uint8_t threadId, const std::string& evbName)
    : WorkerThread(folly::EventBaseManager::get(), evbName),
      nextRequestId_(static_cast<uint64_t>(threadId) << 56),
      callback_(callback) {
}

uint64_t RequestWorkerThread::nextRequestId() {
  return getRequestWorkerThread()->nextRequestId_++;
}

void RequestWorkerThread::flushStats() {
  CHECK(getEventBase()->isInEventBaseThread());
  for (auto& p: serviceWorkers_) {
    p.second->flushStats();
  }
}

void RequestWorkerThread::setup() {
  WorkerThread::setup();
  callback_.workerStarted(this);
}

void RequestWorkerThread::cleanup() {
  WorkerThread::cleanup();
  callback_.workerFinished(this);
}

} // proxygen
