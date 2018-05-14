/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKDecoderBase.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>

namespace proxygen {

uint32_t HPACKDecoderBase::emit(const HPACKHeader& header, headers_t* emitted) {
  if (streamingCb_) {
    streamingCb_->onHeader(header.name.get(), header.value);
  } else if (emitted) {
    // copying HPACKHeader
    emitted->emplace_back(header.name.get(), header.value);
  }
  return header.bytes();
}

void HPACKDecoderBase::completeDecode(uint32_t compressedSize,
                                      uint32_t emittedSize) {
  if (err_ != HPACK::DecodeError::NONE) {
    if (streamingCb_->stats) {
      if (err_ == HPACK::DecodeError::HEADERS_TOO_LARGE ||
          err_ == HPACK::DecodeError::LITERAL_TOO_LARGE) {
        streamingCb_->stats->recordDecodeTooLarge(HeaderCodec::Type::HPACK);
      } else {
        streamingCb_->stats->recordDecodeError(HeaderCodec::Type::HPACK);
      }
    }
    streamingCb_->onDecodeError(hpack2headerCodecError(err_));
  } else {
    HTTPHeaderSize decodedSize;
    decodedSize.compressed = compressedSize;
    decodedSize.uncompressed = emittedSize;
    if (streamingCb_->stats) {
      streamingCb_->stats->recordDecode(HeaderCodec::Type::HPACK, decodedSize);
    }
    streamingCb_->onHeadersComplete(decodedSize);
  }
}

void HPACKDecoderBase::handleTableSizeUpdate(HPACKDecodeBuffer& dbuf,
                                             HeaderTable& table) {
  uint32_t arg = 0;
  err_ = dbuf.decodeInteger(HPACK::TABLE_SIZE_UPDATE.prefixLength, arg);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding maxSize err_=" << err_;
    return;
  }

  if (arg > maxTableSize_) {
    LOG(ERROR) << "Tried to increase size of the header table";
    err_ = HPACK::DecodeError::INVALID_TABLE_SIZE;
    return;
  }
  VLOG(5) << "Received table size update, new size=" << arg;
  table.setCapacity(arg);
}

}
