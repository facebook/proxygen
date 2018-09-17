/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/QPACKContext.h>

namespace proxygen {

QPACKContext::QPACKContext(uint32_t tableSize, bool trackReferences) :
    table_(tableSize, trackReferences) {
}

const HPACKHeader& QPACKContext::getHeader(bool isStatic,
                                           uint32_t index,
                                           uint32_t base,
                                           bool aboveBase) {
  if (isStatic) {
    return getStaticTable().getHeader(index);
  }
  if (aboveBase) {
    base += index;
    index = 1;
  }
  return table_.getHeader(index, base);
}

void QPACKContext::seedHeaderTable(
  std::vector<HPACKHeader>& headers) {
  for (auto& header: headers) {
    table_.add(std::move(header));
  }
}

void QPACKContext::describe(std::ostream& os) const {
  os << table_;
}

std::ostream& operator<<(std::ostream& os, const QPACKContext& context) {
  context.describe(os);
  return os;
}

}
