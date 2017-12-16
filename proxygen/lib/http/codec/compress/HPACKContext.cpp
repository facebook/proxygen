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
#include <proxygen/lib/http/codec/compress/HPACKHeaderTableImpl.h>
#include <proxygen/lib/http/codec/compress/QCRAMHeader.h>

#include <folly/io/IOBuf.h>

using std::string;

namespace proxygen {

HPACKContext::HPACKContext(uint32_t tableSize, bool qcram, bool useBaseIndex) :
    table_(std::unique_ptr<TableImpl>(
             (qcram ?
              (TableImpl*)new QCRAMTableImpl() :
              (TableImpl*)new HPACKHeaderTableImpl())), tableSize),
    useBaseIndex_(useBaseIndex) {
  table_.setAbsoluteIndexing(useBaseIndex);
}

uint32_t HPACKContext::getIndex(const HPACKHeader& header, int32_t commitEpoch,
                                int32_t curEpoch) const {
  // First consult the static header table if applicable
  // Applicability is determined by the following guided optimizations:
  // 1) The set of CommonHeaders includes all StaticTable headers and so we can
  // quickly conclude that we need not check the StaticTable
  // for non-CommonHeaders
  // 2) The StaticTable only contains non empty values for a very small subset
  // of header names.  As getIndex is only meaingful if both name and value
  // match, we know that if our header has a value and is not part of the very
  // small subset of header names, there is no point consulting the StaticTable
  bool consultStaticTable = false;
  if (header.value.empty()) {
    consultStaticTable = header.name.isCommonHeader();
  } else {
    consultStaticTable = StaticHeaderTable::isHeaderCodeInTableWithNonEmptyValue(
      header.name.getHeaderCode());
  }
  if (consultStaticTable) {
    uint32_t staticIndex = getStaticTable().getIndex(header);
    if (staticIndex) {
      return staticToGlobalIndex(staticIndex);
    }
  }

  // Else check the dynamic table
  uint32_t dynamicIndex = table_.getIndex(header, commitEpoch, curEpoch);
  if (dynamicIndex && dynamicIndex != std::numeric_limits<uint32_t>::max()) {
    return dynamicToGlobalIndex(dynamicIndex);
  } else {
    return dynamicIndex;
  }
}

uint32_t HPACKContext::nameIndex(const HPACKHeaderName& headerName,
                                 int32_t commitEpoch,
                                 int32_t curEpoch) const {
  uint32_t index = getStaticTable().nameIndex(headerName);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.nameIndex(headerName, commitEpoch, curEpoch);
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
