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

#include <proxygen/lib/http/codec/compress/HPACKCodec.h>

namespace proxygen {

class HPACKCodec09 : public HPACKCodec {
 public:
  explicit HPACKCodec09(TransportDirection direction);
};

}
