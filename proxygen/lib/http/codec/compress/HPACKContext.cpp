/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKContext.h>

#include <folly/io/IOBuf.h>

using std::string;

namespace proxygen {

HPACKContext::HPACKContext(uint32_t tableSize) :
    table_(tableSize) {
}

uint32_t HPACKContext::getIndex(const HPACKHeader& header) const {
  uint32_t index = getStaticTable().getIndex(header);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.getIndex(header);
  if (index) {
    return dynamicToGlobalIndex(index);
  }
  return 0;
}

uint32_t HPACKContext::nameIndex(const string& name) const {
  uint32_t index = getStaticTable().nameIndex(name);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.nameIndex(name);
  if (index) {
    return dynamicToGlobalIndex(index);
  }
  return 0;
}

bool HPACKContext::isStatic(uint32_t index) const {
  return index <= getStaticTable().size();
}

const HPACKHeader& HPACKContext::getStaticHeader(uint32_t index) {
  DCHECK(isStatic(index));
  return getStaticTable()[globalToStaticIndex(index)];
}

const HPACKHeader& HPACKContext::getDynamicHeader(uint32_t index) {
  DCHECK(!isStatic(index));
  return table_[globalToDynamicIndex(index)];
}

const HPACKHeader& HPACKContext::getHeader(uint32_t index) {
  if (isStatic(index)) {
    return getStaticHeader(index);
  }
  return getDynamicHeader(index);
}

void HPACKContext::seedHeaderTable(
  std::vector<HPACKHeader>& headers) {
  for (const auto& header: headers) {
    table_.add(header);
  }
}

void HPACKContext::describe(std::ostream& os) const {
  os << table_;
}

std::ostream& operator<<(std::ostream& os, const HPACKContext& context) {
  context.describe(os);
  return os;
}

}
