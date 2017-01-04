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
#include <vector>

namespace proxygen {

struct TraceEventObserver;
class TraceEvent;

class TraceEventContext {
 public:
  // Optional parent id for all sub trace events to add.
  uint32_t parentID;

  TraceEventContext(uint32_t pID, std::vector<TraceEventObserver*> observers)
      : parentID(pID), observers_(std::move(observers)) {}

  explicit TraceEventContext(uint32_t pID = 0,
                             TraceEventObserver* observer = nullptr)
      : parentID(pID) {
    if (observer) {
      observers_.push_back(observer);
    }
  }

  void traceEventAvailable(TraceEvent event);

 private:
  // Observer vector to observe all trace events about to occur
  std::vector<TraceEventObserver*> observers_;
};

}
