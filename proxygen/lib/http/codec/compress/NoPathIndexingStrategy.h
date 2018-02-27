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

#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>

namespace proxygen {

class NoPathIndexingStrategy : public HeaderIndexingStrategy {
 public:
  static const NoPathIndexingStrategy* getInstance();

  NoPathIndexingStrategy()
    : HeaderIndexingStrategy() {}

  // For compression simulations we do not want to index :path headers
  bool indexHeader(const HPACKHeader& header) const override {
    if (header.name.getHeaderCode() == HTTP_HEADER_COLON_PATH) {
      return false;
    } else {
      return HeaderIndexingStrategy::indexHeader(header);
    }
  }
};

}
