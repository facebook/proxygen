/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/services/RequestWorker.h>

#include <folly/io/async/EventBaseManager.h>
#include <proxygen/lib/services/ServiceWorker.h>

namespace proxygen {

RequestWorker::RequestWorker(FinishCallback& callback, uint8_t threadId)
    : WorkerThread(folly::EventBaseManager::get()),
      nextRequestId_(static_cast<uint64_t>(threadId) << 56),
      callback_(callback) {
}

uint64_t RequestWorker::nextRequestId() {
  return getRequestWorker()->nextRequestId_++;
}

void RequestWorker::flushStats() {
  CHECK(getEventBase()->isInEventBaseThread());
  for (auto& p: serviceWorkers_) {
    p.second->flushStats();
  }
}

void RequestWorker::setup() {
  WorkerThread::setup();
  callback_.workerStarted(this);
}

void RequestWorker::cleanup() {
  WorkerThread::cleanup();
  callback_.workerFinished(this);
}

} // proxygen
