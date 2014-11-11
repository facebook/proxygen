/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>

#include <algorithm>
#include <folly/Memory.h>

using folly::IOBuf;
using folly::io::Cursor;
using std::list;
using std::string;
using std::unique_ptr;
using std::vector;

namespace proxygen {

unique_ptr<HPACKDecoder::headers_t> HPACKDecoder::decode(const IOBuf* buffer) {
  auto headers = folly::make_unique<headers_t>();
  Cursor cursor(buffer);
  uint32_t totalBytes = buffer ? cursor.totalLength() : 0;
  decode(cursor, totalBytes, *headers);
  // release ownership of the set of headers
  return std::move(headers);
}

uint32_t HPACKDecoder::decode(Cursor& cursor,
                              uint32_t totalBytes,
                              headers_t& headers) {
  HPACKDecodeBuffer dbuf(msgType_, cursor, totalBytes);
  while (!hasError() && !dbuf.empty()) {
    decodeHeader(dbuf, headers);
  }
  emitRefset(headers);
  return dbuf.consumedBytes();
}

void HPACKDecoder::emitRefset(headers_t& emitted) {
  // emit the reference set
  std::sort(emitted.begin(), emitted.end());
  list<uint32_t> refset = table_.referenceSet();
  // remove the refset entries that have already been emitted
  list<uint32_t>::iterator refit = refset.begin();
  while (refit != refset.end()) {
    const HPACKHeader& header = table_[*refit];
    if (std::binary_search(emitted.begin(), emitted.end(), header)) {
      refit = refset.erase(refit);
    } else {
      refit++;
    }
  }
  // try to avoid multiple resizing of the headers vector
  emitted.reserve(emitted.size() + refset.size());
  for (const auto& index : refset) {
    emit(table_[index], emitted);
  }
}

void HPACKDecoder::decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t& emitted) {
  uint8_t byte = dbuf.peek();
  bool indexing = !(byte & HPACK::HeaderEncoding::LITERAL_NO_INDEXING);
  HPACKHeader header;
  // check for indexed name
  const uint8_t indexMask = 0x3F;  // 0011 1111
  if (byte & indexMask) {
    uint32_t index;
    if (!dbuf.decodeInteger(6, index)) {
      LOG(ERROR) << "buffer overflow decoding index";
      err_ = Error::BUFFER_OVERFLOW;
      return;
    }
    // validate the index
    if (!isValid(index)) {
      LOG(ERROR) << "received invalid index: " << index;
      err_ = Error::INVALID_INDEX;
      return;
    }
    header.name = getHeader(index).name;
  } else {
    // skip current byte
    dbuf.next();
    if (!dbuf.decodeLiteral(header.name)) {
      LOG(ERROR) << "buffer overflow decoding header name";
      err_ = Error::BUFFER_OVERFLOW;
      return;
    }
  }
  // value
  if (!dbuf.decodeLiteral(header.value)) {
    LOG(ERROR) << "buffer overflow decoding header value";
    err_ = Error::BUFFER_OVERFLOW;
    return;
  }

  emit(header, emitted);

  if (indexing && table_.add(header)) {
    // only add it to the refset if the header fit in the table
    table_.addReference(1);
  }
}

void HPACKDecoder::decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t& emitted) {
  uint32_t index;
  if (!dbuf.decodeInteger(7, index)) {
    LOG(ERROR) << "buffer overflow decoding index";
    err_ = Error::BUFFER_OVERFLOW;
    return;
  }
  if (index == 0) {
    table_.clearReferenceSet();
    return;
  }
  // validate the index
  if (!isValid(index)) {
    LOG(ERROR) << "received invalid index: " << index;
    err_ = Error::INVALID_INDEX;
    return;
  }
  // a static index cannot be part of the reference set
  if (isStatic(index)) {
    auto& header = StaticHeaderTable::get()[index - table_.size()];
    emit(header, emitted);
    if (table_.add(header)) {
      table_.addReference(1);
    }
  } else if (table_.inReferenceSet(index)) {
    // index remove operation
    table_.removeReference(index);
  } else {
    auto& header = table_[index];
    emit(header, emitted);
    table_.addReference(index);
  }
}

bool HPACKDecoder::isValid(uint32_t index) {
  if (index <= table_.size()) {
    return table_.isValid(index);
  }
  return StaticHeaderTable::get().isValid(index - table_.size());
}

void HPACKDecoder::decodeHeader(HPACKDecodeBuffer& dbuf, headers_t& emitted) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::HeaderEncoding::INDEXED) {
    decodeIndexedHeader(dbuf, emitted);
  } else  {
    // LITERAL_NO_INDEXING or LITERAL_INCR_INDEXING
    decodeLiteralHeader(dbuf, emitted);
  }
}

void HPACKDecoder::emit(const HPACKHeader& header,
                        headers_t& emitted) {
  emitted.push_back(header);
}

}
