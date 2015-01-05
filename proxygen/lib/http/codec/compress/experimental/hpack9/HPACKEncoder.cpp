/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKEncoder.h>

#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKConstants.h>

#include <folly/io/IOBuf.h>

namespace proxygen {

std::unique_ptr<folly::IOBuf> HPACKEncoder09::encode(
  const std::vector<HPACKHeader>& headers,
  uint32_t headroom) {
  if (headroom) {
    buffer_.addHeadroom(headroom);
    headroom = 0;
  }
  if (pendingContextUpdate_) {
    buffer_.encodeInteger(table_.capacity(),
                          HPACK09::HeaderEncoding::TABLE_SIZE_UPDATE,
                          5);
    pendingContextUpdate_ = false;
  }
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  return buffer_.release();
}

void HPACKEncoder09::encodeHeader(const HPACKHeader& header) {
  uint32_t index = getIndex(header);
  if (index) {
    encodeAsIndex(index);
  } else {
    encodeAsLiteral(header);
  }
}

void HPACKEncoder09::encodeAsLiteral(const HPACKHeader& header) {
  bool indexing = header.isIndexable();
  uint8_t prefix = indexing ?
    HPACK09::HeaderEncoding::LITERAL_INCR_INDEXING :
    HPACK09::HeaderEncoding::LITERAL_NO_INDEXING;
  uint8_t len = indexing ? 6 : 4;
  // name
  uint32_t index = nameIndex(header.name);
  if (index) {
    buffer_.encodeInteger(index, prefix, len);
  } else {
    buffer_.encodeInteger(0, prefix, len);
    buffer_.encodeLiteral(header.name);
  }
  // value
  buffer_.encodeLiteral(header.value);
  // indexed ones need to get added to the header table
  if (indexing) {
    table_.add(header);
  }
}

}
