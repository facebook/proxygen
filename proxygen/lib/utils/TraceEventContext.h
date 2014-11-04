/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstdint>

namespace proxygen {

struct TraceEventObserver;

struct TraceEventContext {
  // Optional parent id for all sub trace events to add.
  uint32_t parentID;

  // Optional observer to observe all trace events about to occur
  TraceEventObserver* observer;

  explicit TraceEventContext(uint32_t pID = 0,
                             TraceEventObserver* ob = nullptr)
    : parentID(pID)
, observer(ob) {
  }
};

}
