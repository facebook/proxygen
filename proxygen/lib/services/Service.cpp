/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/services/Service.h>

#include <proxygen/lib/services/RequestWorker.h>
#include <proxygen/lib/services/ServiceWorker.h>

namespace proxygen {

Service::Service() {
}

Service::~Service() {
}

void Service::addServiceWorker(std::unique_ptr<ServiceWorker> worker,
                               RequestWorker* reqWorker) {
  reqWorker->addServiceWorker(this, worker.get());
  workers_.emplace_back(std::move(worker));
}

void Service::clearServiceWorkers() {
  workers_.clear();
}

} // proxygen
