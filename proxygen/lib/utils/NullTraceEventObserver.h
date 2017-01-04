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

#include <proxygen/lib/utils/TraceEventObserver.h>

namespace proxygen {
/*
 * A no-op trace event observer
 */
struct NullTraceEventObserver : public TraceEventObserver {
  void traceEventAvailable(TraceEvent) noexcept override {}
};

}
