/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/utils/TraceEvent.h>

namespace proxygen {

/*
 * Obersver interface to record trace events.
 */
struct TraceEventObserver {
  virtual ~TraceEventObserver() {}
  virtual void traceEventAvailable(TraceEvent event) noexcept {}
};

}
