/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

namespace proxygen {

std::ostream& operator<<(std::ostream& os, const HPACKHeader& h) {
  os << h.name << ": " << h.value;
  return os;
}

}
