/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMContext.h>

#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/codec/compress/HPACKHeaderTableImpl.h>

using std::string;

namespace proxygen {

QCRAMContext::QCRAMContext(uint32_t tableSize)
    : table_(std::unique_ptr<QCRAMNewTableImpl>(new QCRAMNewTableImpl()),
             tableSize),
      useBaseIndex_(true) {
  table_.setAbsoluteIndexing(true);
}

uint32_t QCRAMContext::getIndex(const HPACKHeader& header,
                                bool allowVulnerable,
                                int32_t commitEpoch,
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
    consultStaticTable =
        StaticHeaderTable::isHeaderCodeInTableWithNonEmptyValue(
            header.name.getHeaderCode());
  }
  if (consultStaticTable) {
    uint32_t staticIndex = getStaticTable().getIndex(header);
    if (staticIndex) {
      return staticToGlobalIndex(staticIndex);
    }
  }

  // Else check the dynamic table
  uint32_t dynamicIndex =
      table_.getIndex(header, allowVulnerable, commitEpoch, curEpoch);
  if (dynamicIndex && dynamicIndex != kMaxIndex) {
    return dynamicToGlobalIndex(dynamicIndex);
  } else {
    return dynamicIndex;
  }
}

uint32_t QCRAMContext::nameIndex(const HPACKHeaderName& headerName,
                                 bool allowVulnerable,
                                 int32_t commitEpoch,
                                 int32_t curEpoch) const {
  uint32_t index = getStaticTable().nameIndex(headerName);
  if (index) {
    return staticToGlobalIndex(index);
  }
  index = table_.nameIndex(headerName, allowVulnerable, commitEpoch, curEpoch);
  if (index) {
    return dynamicToGlobalIndex(index);
  }
  return 0;
}

bool QCRAMContext::isStatic(uint32_t index) const {
  return index <= getStaticTable().size();
}

const HPACKHeader& QCRAMContext::getStaticHeader(uint32_t index) {
  DCHECK(isStatic(index));
  return getStaticTable().getHeader(globalToStaticIndex(index));
}

const HPACKHeader& QCRAMContext::getDynamicHeader(uint32_t index) {
  DCHECK(!isStatic(index));
  uint32_t dynamicIndex = globalToDynamicIndex(index);
  VLOG(1) << "getDynamicHeader index " << dynamicIndex;
  return table_.getHeader(dynamicIndex);
}

const HPACKHeader& QCRAMContext::getHeader(uint32_t index) {
  if (isStatic(index)) {
    return getStaticHeader(index);
  }
  return getDynamicHeader(index);
}

bool QCRAMContext::isVulnerable(uint32_t index,
                                int32_t commitEpoch,
                                int32_t packetEpoch) {
  if (isStatic(index)) {
    return false;
  }
  uint32_t dynamicIndex = globalToDynamicIndex(index);
  bool result =
      table_.isVulnerableEpoch(dynamicIndex, commitEpoch, packetEpoch);
  if (result) {
    VLOG(1) << "index " << dynamicIndex << " is vulnerable";
  }
  return table_.isVulnerableEpoch(dynamicIndex, commitEpoch, packetEpoch);
}

void QCRAMContext::seedHeaderTable(std::vector<HPACKHeader>& headers) {
  for (const auto& header : headers) {
    table_.add(header);
  }
}

void QCRAMContext::describe(std::ostream& os) const {
  os << table_;
}

std::ostream& operator<<(std::ostream& os, const QCRAMContext& context) {
  context.describe(os);
  return os;
}

} // namespace proxygen
