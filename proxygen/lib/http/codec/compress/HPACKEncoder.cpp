/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>

using std::vector;

namespace proxygen {

std::unique_ptr<folly::IOBuf>
HPACKEncoder::encode(const vector<HPACKHeader>& headers, uint32_t headroom) {
  if (headroom) {
    streamBuffer_.addHeadroom(headroom);
  }
  handlePendingContextUpdate(streamBuffer_, table_.capacity());
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  return streamBuffer_.release();
}

bool HPACKEncoder::encodeAsLiteral(const HPACKHeader& header, bool indexing) {
  if (header.bytes() > table_.capacity()) {
    // May want to investigate further whether or not this is wanted.
    // Flushing the table on a large header frees up some memory,
    // however, there will be no compression due to an empty table, and
    // the table will fill up again fairly quickly
    indexing = false;
  }

  HPACK::Instruction instruction = (indexing) ?
    HPACK::LITERAL_INC_INDEX : HPACK::LITERAL;

  encodeLiteral(header, nameIndex(header.name), instruction);
  // indexed ones need to get added to the header table
  if (indexing) {
    CHECK(table_.add(header.copy()));
  }
  return true;
}

void HPACKEncoder::encodeLiteral(const HPACKHeader& header,
                                 uint32_t nameIndex,
                                 const HPACK::Instruction& instruction) {
  // name
  if (nameIndex) {
    VLOG(10) << "encoding name index=" << nameIndex;
    streamBuffer_.encodeInteger(nameIndex, instruction);
  } else {
    streamBuffer_.encodeInteger(0, instruction);
    streamBuffer_.encodeLiteral(header.name.get());
  }
  // value
  streamBuffer_.encodeLiteral(header.value);
}

void HPACKEncoder::encodeAsIndex(uint32_t index) {
  VLOG(10) << "encoding index=" << index;
  streamBuffer_.encodeInteger(index, HPACK::INDEX_REF);
}

void HPACKEncoder::encodeHeader(const HPACKHeader& header) {
  // First determine whether the header is defined as indexable using the
  // set strategy if applicable, else assume it is indexable
  bool indexable = !indexingStrat_ || indexingStrat_->indexHeader(header);

  // If the header was not defined as indexable, its a reasonable assumption
  // that it does not appear in either the static or dynamic table and should
  // not be searched.  The only time this is not true is if the header indexing
  // strat specified an exact header/value pair that is in the static header
  // table although semantically the header indexing strategy should indeed act
  // as an override so we assume this is desired if such a case occurs
  uint32_t index = 0;
  if (indexable) {
    index = getIndex(header);
  }

  // Finally encode the header as determined above
  if (index) {
    encodeAsIndex(index);
  } else {
    encodeAsLiteral(header, indexable);
  }
}

}
