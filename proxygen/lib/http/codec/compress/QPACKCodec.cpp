/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/QPACKCodec.h>

#include <algorithm>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h> // for prepareHeaders
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
      decoder_(HPACK::kTableSize, maxUncompressed_) {}

void QPACKCodec::recordCompressedSize(
  const QPACKEncoder::EncodeResult& encodeRes) {
  encodedSize_.compressed = 0;
  if (encodeRes.control) {
    encodedSize_.compressed += encodeRes.control->computeChainDataLength();
  }
  if (encodeRes.stream) {
    encodedSize_.compressed += encodeRes.stream->computeChainDataLength();
  }
  if (stats_) {
    stats_->recordEncode(Type::QPACK, encodedSize_);
  }
}

QPACKEncoder::EncodeResult QPACKCodec::encode(
  vector<Header>& headers, uint64_t streamId) noexcept {
  auto prepared = compress::prepareHeaders(headers);
  encodedSize_.uncompressed = prepared.second;
  auto res = encoder_.encode(prepared.first, encodeHeadroom_, streamId);
  recordCompressedSize(res);
  return res;
}

void QPACKCodec::decodeStreaming(
    uint64_t streamID,
    std::unique_ptr<folly::IOBuf> block,
    uint32_t length,
    HPACK::StreamingCallback* streamingCb) noexcept {
  if (streamingCb) {
    streamingCb->stats = stats_;
  }
  decoder_.decodeStreaming(streamID, std::move(block), length, streamingCb);
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
