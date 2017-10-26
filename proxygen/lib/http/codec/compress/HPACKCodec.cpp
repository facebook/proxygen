/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
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

HPACKCodec::HPACKCodec(TransportDirection /*direction*/,
                       bool emitSequenceNumbers,
                       bool useBaseIndex,
                       bool autoCommit)
    : encoder_(true, HPACK::kTableSize, emitSequenceNumbers, useBaseIndex,
               autoCommit),
      decoder_(HPACK::kTableSize, maxUncompressed_, useBaseIndex) {}

unique_ptr<IOBuf> HPACKCodec::encode(vector<Header>& headers) noexcept {
  bool eviction = false;
  return encode(headers, eviction);
}

unique_ptr<IOBuf> HPACKCodec::encode(vector<Header>& headers,
                                     bool& eviction) noexcept {
  // convert to HPACK API format
  vector<HPACKHeader> converted;
  converted.reserve(headers.size());
  uint32_t uncompressed = 0;
  for (const auto& h : headers) {
    // HPACKHeader automatically lowercases
    converted.emplace_back(*h.name, *h.value);
    auto& header = converted.back();
    uncompressed += header.name.size() + header.value.size() + 2;
  }
  auto buf = encoder_.encode(converted, encodeHeadroom_, &eviction);
  encodedSize_.compressed = 0;
  if (buf) {
    encodedSize_.compressed = buf->computeChainDataLength();
  }
  encodedSize_.uncompressed = uncompressed;
  if (stats_) {
    stats_->recordEncode(Type::HPACK, encodedSize_);
  }
  return buf;
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
  decodedSize_.compressed = consumed;
  decodedSize_.uncompressed = uncompressed;
  if (stats_) {
    stats_->recordDecode(Type::HPACK, decodedSize_);
  }
  return HeaderDecodeResult{outHeaders_, consumed};
}

void HPACKCodec::decodeStreaming(
    Cursor& cursor,
    uint32_t length,
    HeaderCodec::StreamingCallback* streamingCb) noexcept {
  decodedSize_.uncompressed = 0;
  streamingCb_ = streamingCb;
  auto consumed = decoder_.decodeStreaming(cursor, length, this);
  if (decoder_.hasError()) {
    onDecodeError(HeaderDecodeError::NONE);
    return;
  }
  decodedSize_.compressed = consumed;
  onHeadersComplete(decodedSize_);
}

void HPACKCodec::onHeader(const folly::fbstring& name,
                          const folly::fbstring& value) {
  assert(streamingCb_ != nullptr);
  decodedSize_.uncompressed += name.size() + value.size() + 2;
  streamingCb_->onHeader(name, value);
}

void HPACKCodec::onHeadersComplete(HTTPHeaderSize decodedSize) {
  assert(streamingCb_ != nullptr);
  if (stats_) {
    stats_->recordDecode(Type::HPACK, decodedSize_);
  }
  streamingCb_->onHeadersComplete(decodedSize);
}

void HPACKCodec::onDecodeError(HeaderDecodeError /*decodeError*/) {
  assert(streamingCb_ != nullptr);
  if (stats_) {
    stats_->recordDecodeError(Type::HPACK);
  }
  streamingCb_->onDecodeError(hpack2headerCodecError(decoder_.getError()));
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
