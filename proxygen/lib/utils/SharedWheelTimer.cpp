/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <proxygen/lib/utils/SharedWheelTimer.h>

#include <folly/io/async/EventBaseManager.h>

#include <folly/Singleton.h>

namespace proxygen {

folly::Singleton<SharedWheelTimer>
  sharedWheelTimerSingleton; // singleton registration

std::weak_ptr<folly::HHWheelTimer> SharedWheelTimer::
  get(folly::EventBase* eventBase) {
  if (eventBase == nullptr) {
    eventBase = folly::EventBaseManager::get()->getEventBase();
  }
  CHECK(eventBase != nullptr) << "eventBase cannot be nullptr, it should be\
                     either attached to current thread or passed in";
  std::lock_guard<std::mutex> lock(timersLock_);
  auto found = timers_.find (eventBase);
  if (found == timers_.end()) {
    return timers_.insert({eventBase, std::shared_ptr<folly::HHWheelTimer>(
      new folly::HHWheelTimer(  // intentionally not specifying timeouts as
                                // those are shared so that there will be
                                // errors in tests on attempts to
                                // scheduleTimeout without value
        eventBase,
        std::chrono::milliseconds(folly::HHWheelTimer::DEFAULT_TICK_INTERVAL),
        folly::AsyncTimeout::InternalEnum::NORMAL),
      [] (folly::HHWheelTimer* s) { s->destroy(); } // need to call destroy
                                                    // since it has delayed
                                                    // destruction
    )}).first->second;
  }
  else {
    return found->second;
  }
}

}
