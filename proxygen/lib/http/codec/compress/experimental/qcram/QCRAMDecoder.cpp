/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMDecoder.h>
// for hpack2headerCodecError
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>

#include <algorithm>
#include <folly/Memory.h>
#include <proxygen/lib/http/codec/compress/Huffman.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMCodec.h>

using folly::IOBuf;
using folly::io::Cursor;
using proxygen::HPACK::DecodeError;
using std::list;
using std::string;
using std::unique_ptr;
using std::vector;

namespace proxygen {
unique_ptr<QCRAMDecoder::headers_t> QCRAMDecoder::decode(const IOBuf* buffer) {
  auto headers = std::make_unique<headers_t>();
  Cursor cursor(buffer);
  uint32_t totalBytes = buffer ? cursor.totalLength() : 0;
  decode(cursor, totalBytes, *headers);
  // release ownership of the set of headers
  return headers;
}

const huffman::HuffTree& QCRAMDecoder::getHuffmanTree() const {
  return huffman::huffTree();
}

void QCRAMDecoder::handleBaseIndex(HPACKDecodeBuffer& dbuf) {
  if (useBaseIndex_) {
    uint32_t baseIndex = 0;
    err_ = dbuf.decodeInteger(8, baseIndex);
    if (err_ != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Decode error decoding maxSize err_=" << err_;
      return;
    }
    table_.setBaseIndex(baseIndex);
  }
}

uint32_t QCRAMDecoder::decode(Cursor& cursor,
                              uint32_t totalBytes,
                              headers_t& headers) {
  uint32_t emittedSize = 0;
  HPACKDecodeBuffer dbuf(
      getHuffmanTree(), cursor, totalBytes, maxUncompressed_);
  handleBaseIndex(dbuf);
  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeader(dbuf, &headers);
    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of " << maxUncompressed_
                 << " bytes";
      err_ = DecodeError::HEADERS_TOO_LARGE;
      return dbuf.consumedBytes();
    }
  }
  return dbuf.consumedBytes();
}

uint32_t QCRAMDecoder::decodeStreaming(
    Cursor& cursor,
    uint32_t totalBytes,
    HeaderCodec::StreamingCallback* streamingCb) {

  uint32_t emittedSize = 0;
  streamingCb_ = streamingCb;
  HPACKDecodeBuffer dbuf(
      getHuffmanTree(), cursor, totalBytes, maxUncompressed_);
  handleBaseIndex(dbuf);
  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeader(dbuf, nullptr);

    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of " << maxUncompressed_
                 << " bytes";
      err_ = HPACK::DecodeError::HEADERS_TOO_LARGE;
      return dbuf.consumedBytes();
    }
  }

  return dbuf.consumedBytes();
}

void QCRAMDecoder::handleTableSizeUpdate(HPACKDecodeBuffer& dbuf) {
  uint32_t arg = 0;
  err_ = dbuf.decodeInteger(5, arg);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding maxSize err_=" << err_;
    return;
  }

  if (arg > maxTableSize_) {
    LOG(ERROR) << "Tried to increase size of the header table";
    err_ = HPACK::DecodeError::INVALID_TABLE_SIZE;
    return;
  }
  table_.setCapacity(arg);
}

void QCRAMDecoder::updateMembership() {
  /* Update membership */
  uint32_t index = table_.writeBaseIndex();
  VLOG(1) << "membership: adding " << index;
  MembershipRange singleton{index, index};
  if (membership_.empty()) {
    membership_.insert(singleton);
    VLOG(1) << "membership: empty insert singleton " << singleton.low;
    return;
  }
  bool merged = false;
  auto it = membership_.upper_bound(singleton);
  if (it != membership_.end()) {
    // This is the first range with low > singleton.low.
    auto right = *it;
    VLOG(1) << "membership: right [" << right.low << "," << right.high << "]";
    if (right.low == singleton.low + 1) {
      // Merge if consecutive with right.
      merged = true;
      singleton.high = right.high;
      VLOG(1) << "membership: erase right";
      size_t before = membership_.size();
      it = membership_.erase(it);
      it--;
      DCHECK_GT(before, membership_.size());
      membership_.insert(singleton);
      VLOG(1) << "membership: merge with right new range [" << singleton.low
              << "," << singleton.high << "]";
    }
  }
  it--;
  auto left = *it;
  VLOG(1) << "membership: left [" << left.low << "," << left.high << "]";
  if (left.high == singleton.low - 1) {
    // Merge if consecutive with left.
    singleton.low = left.low;
    VLOG(1) << "membership: erase left";
    size_t before = membership_.size();
    it = membership_.erase(it);
    if (merged) {
      membership_.erase(it);
    }
    DCHECK_GT(before, membership_.size());
    membership_.insert(singleton);
    VLOG(1) << "membership: merge with left new range [" << singleton.low << ","
            << singleton.high << "]";
    merged = true;
  }
  if (merged) {
    return;
  }
  VLOG(1) << "membership: insert singleton " << singleton.low;
  membership_.insert(singleton);
  VLOG(1) << "membership size now " << membership_.size();
}

bool QCRAMDecoder::dependsOK(uint32_t depends) const {
  if (membership_.empty()) {
    return false;
  }
  auto it = membership_.cbegin();
  return (it->low == 1 && it->high >= depends);
}

uint32_t QCRAMDecoder::decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                           headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  bool indexing = byte & HPACK::HeaderEncoding::LITERAL_INCR_INDEXING;
  QCRAMHeader header;
  uint8_t indexMask = 0x3F; // 0011 1111
  uint8_t length = 6;
  if (!indexing) {
    bool tableSizeUpdate = byte & HPACK::HeaderEncoding::TABLE_SIZE_UPDATE;
    if (tableSizeUpdate) {
      handleTableSizeUpdate(dbuf);
      return 0;
    } else {
      bool neverIndex = byte & HPACK::HeaderEncoding::LITERAL_NEVER_INDEXING;
      // TODO: we need to emit this flag with the headers
    }
    indexMask = 0x0F; // 0000 1111
    length = 4;
  }
  if (byte & indexMask) {
    uint32_t index;
    err_ = dbuf.decodeInteger(length, index);
    if (err_ != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Decode error decoding index err_=" << err_;
      return 0;
    }
    // validate the index
    if (!isValid(index)) {
      LOG(ERROR) << "received invalid index: " << index;
      err_ = HPACK::DecodeError::INVALID_INDEX;
      return 0;
    }
    header.name = getHeader(index).name;
  } else {
    // skip current byte
    dbuf.next();
    folly::fbstring headerName;
    err_ = dbuf.decodeLiteral(headerName);
    header.name = headerName;
    if (err_ != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Error decoding header name err_=" << err_;
      return 0;
    }
  }
  // value
  err_ = dbuf.decodeLiteral(header.value);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Error decoding header value name=" << header.name
               << " err_=" << err_;
    return 0;
  }

  uint32_t emittedSize = emit(header, emitted);

  if (indexing) {
    // epoch not really relevant to decoder
    table_.add(header);
    updateMembership();
  }

  return emittedSize;
}

uint32_t QCRAMDecoder::decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                           headers_t* emitted) {
  uint32_t index;
  err_ = dbuf.decodeInteger(7, index);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding index err_=" << err_;
    return 0;
  }
  // validate the index
  if (index == 0 || !isValid(index)) {
    LOG(ERROR) << "received invalid index: " << index;
    err_ = HPACK::DecodeError::INVALID_INDEX;
    return 0;
  }
  uint32_t emittedSize = 0;

  VLOG(1) << "decode indexed header at index " << index;

  if (isStatic(index)) {
    auto& header = getStaticHeader(index);
    emittedSize = emit(header, emitted);
  } else {
    auto& header = getDynamicHeader(index);
    emittedSize = emit(header, emitted);
  }
  return emittedSize;
}

bool QCRAMDecoder::isValid(uint32_t index) {
  if (!isStatic(index)) {
    return table_.isValid(globalToDynamicIndex(index));
  }
  return getStaticTable().isValid(globalToStaticIndex(index));
}

uint32_t QCRAMDecoder::decodeHeader(HPACKDecodeBuffer& dbuf,
                                    headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::HeaderEncoding::INDEXED) {
    return decodeIndexedHeader(dbuf, emitted);
  }
  // LITERAL_NO_INDEXING or LITERAL_INCR_INDEXING
  return decodeLiteralHeader(dbuf, emitted);
}

uint32_t QCRAMDecoder::emit(const HPACKHeader& header, headers_t* emitted) {
  VLOG(1) << "emit: " << header;
  if (streamingCb_) {
    streamingCb_->onHeader(header.name.get(), header.value);
  } else if (emitted) {
    // copying QCRAMHeader
    emitted->emplace_back(header.name.get(), header.value);
  }
  return header.bytes();
}

} // namespace proxygen
