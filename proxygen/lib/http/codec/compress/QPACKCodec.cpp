/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/compress/QPACKCodec.h>

#include <algorithm>
#include <folly/ThreadLocal.h>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h> // for prepareHeaders
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <iosfwd>

using proxygen::compress::Header;
using std::vector;

namespace proxygen {

QPACKCodec::QPACKCodec()
    // by default dynamic tables are 0 size
    : encoder_(true, 0),
      decoder_(0, maxUncompressed_) {}

void QPACKCodec::recordCompressedSize(
  const QPACKEncoder::EncodeResult& encodeRes) {
  encodedSize_.compressed = 0;
  encodedSize_.compressedBlock = 0;
  if (encodeRes.control) {
    encodedSize_.compressed += encodeRes.control->computeChainDataLength();
  }
  if (encodeRes.stream) {
    encodedSize_.compressedBlock = encodeRes.stream->computeChainDataLength();
    encodedSize_.compressed += encodedSize_.compressedBlock;
  }
  if (stats_) {
    stats_->recordEncode(Type::QPACK, encodedSize_);
  }
}

QPACKEncoder::EncodeResult QPACKCodec::encode(
    vector<Header>& headers,
    uint64_t streamId,
    uint32_t maxEncoderStreamBytes) noexcept {
  folly::ThreadLocal<std::vector<HPACKHeader>> preparedTL;
  auto& prepared = *preparedTL.get();
  encodedSize_.uncompressed = compress::prepareHeaders(headers, prepared);
  auto res = encoder_.encode(prepared, encodeHeadroom_, streamId,
                             maxEncoderStreamBytes);
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
