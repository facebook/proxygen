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

#include <cstdint>
#include <map>
#include <proxygen/lib/services/WorkerThread.h>

namespace proxygen {

class Service;
class ServiceWorker;

/**
 * RequestWorker extends WorkerThread, and also contains a list of
 * ServiceWorkers running in this thread.
 */
class RequestWorker : public WorkerThread {
 public:
  class FinishCallback {
   public:
    virtual ~FinishCallback() noexcept {}
    virtual void workerStarted(RequestWorker*) = 0;
    virtual void workerFinished(RequestWorker*) = 0;
  };

  /**
   * Create a new RequestWorker.
   *
   * @param proxygen  The object to notify when this worker finishes.
   * @param threadId  A unique ID for this worker.
   */
  RequestWorker(FinishCallback& callback, uint8_t threadId);

  static uint64_t nextRequestId();

  static RequestWorker* getRequestWorker() {
    RequestWorker* self = dynamic_cast<RequestWorker*>(
      WorkerThread::getCurrentWorkerThread());
    CHECK_NOTNULL(self);
    return self;
  }

  /**
   * Track the ServiceWorker objects in-use by this worker.
   */
  void addServiceWorker(Service* service, ServiceWorker* sw) {
    CHECK(serviceWorkers_.find(service) == serviceWorkers_.end());
    serviceWorkers_[service] = sw;
  }

  /**
   * For a given service, returns the ServiceWorker associated with this
   * RequestWorker
   */
  ServiceWorker* getServiceWorker(Service* service) const {
    auto it = serviceWorkers_.find(service);
    CHECK(it != serviceWorkers_.end());
    return it->second;
  }

  /**
   * Flush any thread-local stats being tracked by our ServiceWorkers.
   *
   * This must be invoked from within worker's thread.
   */
  void flushStats();

 private:
  void setup() override;
  void cleanup() override;

  // The next request id within this thread. The id has its highest byte set to
  // the thread id, so is unique across the process.
  uint64_t nextRequestId_;

  // The ServiceWorkers executing in this worker
  std::map<Service*, ServiceWorker*> serviceWorkers_;

  FinishCallback& callback_;
};

}
