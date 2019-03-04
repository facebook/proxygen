/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>

namespace proxygen {

class QPACKStaticHeaderTable {

 public:

  static const StaticHeaderTable& get();

  // Not currently used
  static bool isHeaderCodeInTableWithNonEmptyValue(HTTPHeaderCode headerCode);
};

}
