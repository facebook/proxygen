/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxygen/lib/http/codec/compress/HPACKContext.h"

#include <folly/io/IOBuf.h>

using std::string;

namespace proxygen {

HPACKContext::HPACKContext(HPACK::MessageType msgType, uint32_t tableSize) :
    table_(tableSize), msgType_(msgType) {
}

uint32_t HPACKContext::getIndex(const HPACKHeader& header) const {
  uint32_t index = table_.getIndex(header);
  if (index) {
    return index;
  }
  index = StaticHeaderTable::get().getIndex(header);
  if (index) {
    return table_.size() + index;
  }
  return 0;
}

uint32_t HPACKContext::nameIndex(const string& name) const {
  uint32_t index = table_.nameIndex(name);
  if (index) {
    return index;
  }
  index = StaticHeaderTable::get().nameIndex(name);
  if (index) {
    return table_.size() + index;
  }
  return 0;
}

bool HPACKContext::isStatic(uint32_t index) const {
  return
    index > table_.size()
    && index <= table_.size() + StaticHeaderTable::get().size();
}

const HPACKHeader& HPACKContext::getHeader(uint32_t index) {
  if (isStatic(index)) {
    return StaticHeaderTable::get()[index - table_.size()];
  }
  return table_[index];
}

}
