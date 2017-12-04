/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKCodec.h>

#include <algorithm>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <iosfwd>

using folly::IOBuf;
using folly::io::Cursor;
using proxygen::compress::Header;
using proxygen::compress::HeaderPiece;
using proxygen::compress::HeaderPieceList;
using std::unique_ptr;
using std::vector;

namespace proxygen {

QPACKCodec::QPACKCodec()
    : encoder_(true, HPACK::kTableSize),
      decoder_(*this, HPACK::kTableSize, maxUncompressed_) {
}

Result<HeaderDecodeResult, HeaderDecodeError>
QPACKCodec::decode(folly::io::Cursor&, uint32_t) noexcept {
  // we have to implement this for now because we inherit from HeaderCodec, but
  // only support the streaming interface for asynchrony.
  return HeaderDecodeError::BAD_ENCODING;
}

QPACKCodec::EncodeResult QPACKCodec::encodeQuic(
  vector<Header>& headers) noexcept {
  vector<HPACKHeader> converted;
  // convert to HPACK API format
  uint32_t uncompressed = 0;
  for (const auto& h : headers) {
    converted.emplace_back(*h.name, *h.value);
    auto& header = converted.back();
    uncompressed += header.name.size() + header.value.size() + 2;
  }
  auto result = encoder_.encode(converted, encodeHeadroom_);
  encodedSize_.compressed = 0;
  if (result.first) {
    encodedSize_.compressed += result.first->computeChainDataLength();
  }
  if (result.second) {
    encodedSize_.compressed += result.second->computeChainDataLength();
  }
  encodedSize_.uncompressed = uncompressed;
  if (stats_) {
    stats_->recordEncode(Type::HPACK, encodedSize_);
  }
  return result;
}

void QPACKCodec::decodeControlStream(Cursor& cursor, uint32_t length) {
  decoder_.decodeControlStream(cursor, length);
}

void QPACKCodec::decodeStreaming(
    Cursor& cursor,
    uint32_t length,
    HeaderCodec::StreamingCallback* streamingCb) noexcept {
  if (!decoder_.decodeStreaming(cursor, length, streamingCb)) {
    holBlockCount_++;
  }
}

void QPACKCodec::describe(std::ostream& stream) const {
  stream << "DecoderTable:\n" << decoder_;
  stream << "EncoderTable:\n" << encoder_;
}

std::ostream& operator<<(std::ostream& os, const QPACKCodec& codec) {
  codec.describe(os);
  return os;
}

}
