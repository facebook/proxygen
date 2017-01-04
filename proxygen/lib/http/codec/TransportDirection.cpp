/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/TransportDirection.h>

#include <ostream>

namespace proxygen {

const char* getTransportDirectionString(TransportDirection dir) {
  switch (dir) {
    case TransportDirection::UPSTREAM: return "upstream";
    case TransportDirection::DOWNSTREAM: return "downstream";
  }
  // unreachable
  return "";
}

TransportDirection operator!(TransportDirection dir) {
  return dir == TransportDirection::DOWNSTREAM ?
    TransportDirection::UPSTREAM : TransportDirection::DOWNSTREAM;
}

std::ostream& operator<<(std::ostream& os, const TransportDirection dir) {
  os << getTransportDirectionString(dir);
  return os;
}

}
