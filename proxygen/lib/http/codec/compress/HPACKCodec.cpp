/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>

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

namespace compress {
  std::pair<vector<HPACKHeader>, uint32_t> prepareHeaders(
      vector<Header>& headers) {
  // convert to HPACK API format
  std::pair<vector<HPACKHeader>, uint32_t> converted;
  converted.first.reserve(headers.size());
  uint32_t uncompressed = 0;
  for (const auto& h : headers) {
    // HPACKHeader automatically lowercases
    converted.first.emplace_back(*h.name, *h.value);
    auto& header = converted.first.back();
    converted.second += header.name.size() + header.value.size() + 2;
  }
  return converted;
}
}

HPACKCodec::HPACKCodec(TransportDirection /*direction*/)
    : encoder_(true, HPACK::kTableSize),
      decoder_(HPACK::kTableSize, maxUncompressed_) {}

unique_ptr<IOBuf> HPACKCodec::encode(vector<Header>& headers) noexcept {
  auto prepared = compress::prepareHeaders(headers);
  encodedSize_.uncompressed = prepared.second;
  auto buf = encoder_.encode(prepared.first, encodeHeadroom_);
  recordCompressedSize(buf.get());
  return buf;
}

void HPACKCodec::recordCompressedSize(
  const IOBuf* stream) {
  encodedSize_.compressed = 0;
  if (stream) {
    encodedSize_.compressed += stream->computeChainDataLength();
  }
  if (stats_) {
    stats_->recordEncode(Type::HPACK, encodedSize_);
  }
}

Result<HeaderDecodeResult, HeaderDecodeError>
HPACKCodec::decode(Cursor& cursor, uint32_t length) noexcept {
  outHeaders_.clear();
  decodedHeaders_.clear();
  auto consumed = decoder_.decode(cursor, length, decodedHeaders_);
  if (decoder_.hasError()) {
    LOG(ERROR) << "decoder state: " << decoder_.getTable();
    LOG(ERROR) << "partial headers: ";
    for (const auto& hdr: decodedHeaders_) {
      LOG(ERROR) << "name=" << hdr.name.c_str()
                 << " value=" << hdr.value.c_str();
    }
    auto err = decoder_.getError();
    if (err == HPACK::DecodeError::HEADERS_TOO_LARGE ||
        err == HPACK::DecodeError::LITERAL_TOO_LARGE) {
      if (stats_) {
        stats_->recordDecodeTooLarge(Type::HPACK);
      }
      return HeaderDecodeError::HEADERS_TOO_LARGE;
    }
    if (stats_) {
      stats_->recordDecodeError(Type::HPACK);
    }
    return HeaderDecodeError::BAD_ENCODING;
  }
  // convert to HeaderPieceList
  uint32_t uncompressed = 0;
  for (uint32_t i = 0; i < decodedHeaders_.size(); i++) {
    const HPACKHeader& h = decodedHeaders_[i];
    // SPDYCodec uses this 'multi-valued' flag to detect illegal duplicates
    // Since HPACK does not preclude duplicates, pretend everything is
    // multi-valued
    bool multiValued = true;
    // one entry for the name and one for the value
    outHeaders_.emplace_back((char *)h.name.c_str(), h.name.size(),
                             false, multiValued);
    outHeaders_.emplace_back((char *)h.value.c_str(), h.value.size(),
                             false, multiValued);
    uncompressed += h.name.size() + h.value.size() + 2;
  }
  HTTPHeaderSize decodedSize;
  decodedSize.compressed = consumed;
  decodedSize.uncompressed = uncompressed;
  if (stats_) {
    stats_->recordDecode(Type::HPACK, decodedSize);
  }
  return HeaderDecodeResult{outHeaders_, consumed};
}

void HPACKCodec::decodeStreaming(
    Cursor& cursor,
    uint32_t length,
    HeaderCodec::StreamingCallback* streamingCb) noexcept {
  streamingCb->stats = stats_;
  decoder_.decodeStreaming(cursor, length, streamingCb);
}

void HPACKCodec::describe(std::ostream& stream) const {
  stream << "DecoderTable:\n" << decoder_;
  stream << "EncoderTable:\n" << encoder_;
}

std::ostream& operator<<(std::ostream& os, const HPACKCodec& codec) {
  codec.describe(os);
  return os;
}

}
