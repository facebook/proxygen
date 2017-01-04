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

#include <chrono>

namespace proxygen {

struct AckLatencyEvent {
  // The byte number that was acknowledged.
  unsigned int byteNo;
  // The latency between sending the byte and receiving the ack for that byte.
  std::chrono::nanoseconds latency;
};

}
