/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <list>
#include <memory>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMContext.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMHeader.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionTypes.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>

namespace proxygen {

using compress::FrameFlags;

// Denote the interval [low,high].  We use this in a set to track the
// ranges of indices present in the table.
class MembershipRange {
 public:
  size_t low;
  size_t high;
};

struct cmpByLow : public std::binary_function<MembershipRange, MembershipRange, bool>
{
    bool operator()(const MembershipRange& lhs, const MembershipRange& rhs) const
    {
        return lhs.low < rhs.low;
    }
};

class QCRAMDecoder : public QCRAMContext {
 public:
  explicit QCRAMDecoder(
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed,
    bool useBaseIndex=false)
      // even if useBaseIndex is true, decoder doesn't need epoch so
      // can use the 'HPACK' table impl
      : QCRAMContext(tableSize, false, useBaseIndex),
        maxTableSize_(tableSize),
        maxUncompressed_(maxUncompressed) {}

  typedef std::vector<QCRAMHeader> headers_t;

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers into the given vector.
   */
  uint32_t decode(folly::io::Cursor& cursor,
                  uint32_t totalBytes,
                  headers_t& headers);

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers and invoke a callback.
   */
  uint32_t decodeStreaming(folly::io::Cursor& cursor,
                           uint32_t totalBytes,
                           HeaderCodec::StreamingCallback* streamingCb);

  /**
   * given a compressed header block as an IOBuf chain, decode all the
   * headers and return them. This is just a convenience wrapper around
   * the API above.
   */
  std::unique_ptr<headers_t> decode(const folly::IOBuf* buffer);

  HPACK::DecodeError getError() const {
    return err_;
  }

  bool hasError() const {
    return err_ != HPACK::DecodeError::NONE;
  }

  void setFrameFlags(FrameFlags flags) {
    frameFlags_ = flags;
  }

  void setHeaderTableMaxSize(uint32_t maxSize) {
    maxTableSize_ = maxSize;
  }

  void setMaxUncompressed(uint32_t maxUncompressed) {
    maxUncompressed_ = maxUncompressed;
  }

  uint32_t getTableSize() const {
    return table_.capacity();
  }

  uint32_t getBytesStored() const {
    return table_.bytes();
  }

  uint32_t getHeadersStored() const {
    return table_.size();
  }

  bool dependsOK(uint32_t depends) const;

 protected:
  bool isValid(uint32_t index);

  virtual const huffman::HuffTree& getHuffmanTree() const;

  uint32_t emit(const HPACKHeader& header, headers_t* emitted);

  void handleBaseIndex(HPACKDecodeBuffer& dbuf);

  virtual uint32_t decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t* emitted);

  virtual uint32_t decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t* emitted);

  uint32_t decodeHeader(HPACKDecodeBuffer& dbuf, headers_t* emitted);

  void handleTableSizeUpdate(HPACKDecodeBuffer& dbuf);

  void updateMembership();

  HPACK::DecodeError err_{HPACK::DecodeError::NONE};
  FrameFlags frameFlags_;
  uint32_t maxTableSize_;
  uint32_t maxUncompressed_;
  HeaderCodec::StreamingCallback* streamingCb_{nullptr};
  // For tracking which table entries have been added so far
  std::set<MembershipRange, cmpByLow> membership_;
};

HeaderDecodeError hpack2headerCodecError(HPACK::DecodeError err);

}
