/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKContext.h>

#include <folly/io/IOBuf.h>

using std::string;

namespace proxygen {

QPACKContext::QPACKContext(uint32_t tableSize) :
    table_(tableSize) {
}

uint32_t QPACKContext::getIndex(const HPACKHeader& header) {
  uint32_t index = getStaticTable().getIndex(header);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.getIndexRef(header);
  if (index) {
    return dynamicToGlobalIndex(index);
  }
  return 0;
}

uint32_t QPACKContext::nameIndex(const HPACKHeaderName& name) {
  uint32_t index = getStaticTable().nameIndex(name);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.nameIndexRef(name);
  if (index) {
    return dynamicToGlobalIndex(index);
  }
  return 0;
}

bool QPACKContext::isStatic(uint32_t index) const {
  return index <= getStaticTable().size();
}

const HPACKHeader& QPACKContext::getStaticHeader(uint32_t index) {
  DCHECK(isStatic(index));
  return getStaticTable()[globalToStaticIndex(index)];
}

QPACKHeaderTable::DecodeFuture QPACKContext::getDynamicHeader(uint32_t index) {
  DCHECK(!isStatic(index));
  return table_.decodeIndexRef(globalToDynamicIndex(index));
}

QPACKHeaderTable::DecodeFuture QPACKContext::getHeader(uint32_t index) {
  if (isStatic(index)) {
    return folly::makeFuture<QPACKHeaderTable::DecodeResult>(
      QPACKHeaderTable::DecodeResult(getStaticHeader(index)));
  }
  return getDynamicHeader(index);
}

void QPACKContext::seedHeaderTable(
  std::vector<HPACKHeader>& headers) {
  // This method doesn't work for QPACK, because more state is required than
  // just the headers to recreate the header table.  For now, just add all
  // headers with explicit indexes starting from 1, and ignore remaining QPACK
  // state.
  uint32_t i = 1;
  for (const auto& header: headers) {
    table_.add(header, i++);
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
