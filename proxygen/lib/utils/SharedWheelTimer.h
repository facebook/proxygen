/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>

#include <folly/io/async/HHWheelTimer.h>

namespace proxygen {

/*
 *  Hosts HHWheelTimer instances per eventBase.
 */
class SharedWheelTimer {
public:

  /*
   * To be used to retrieve shared HHWHeelTimer instance, each instance does
   * not have defaultTimeout value. This is intential since instances are
   * shared. If eventBase is nullptr then it will attempt to retrieve eventBase
   * instance attached to this thread.
   * returns weak_ptr so that WheelTimerInstances will not hold destruction
   * when process dies.
   */
  std::weak_ptr<folly::HHWheelTimer>
    get(folly::EventBase* eventBase = nullptr);

private:

  // eventBase has 1:1 mapping with Shared HHWHeelTimer, please note that each
  // eventBase attached to only 1 thread
  std::unordered_map<folly::EventBase*,
                     std::shared_ptr<folly::HHWheelTimer>> timers_;
  std::mutex timersLock_;
};

}
