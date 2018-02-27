/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <string>
#include <vector>

namespace proxygen {

class StaticHeaderTable : public HeaderTable {

 public:
  explicit StaticHeaderTable(const char* entries[][2], int size);

  static const StaticHeaderTable& get();

  static bool isHeaderCodeInTableWithNonEmptyValue(HTTPHeaderCode headerCode);
};

}
