/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/utils/ZstdStreamCompressor.h"

#include <folly/compression/Compression.h>

namespace proxygen {

ZstdStreamCompressor::ZstdStreamCompressor(int compressionLevel)
    : codec_(folly::io::getStreamCodec(folly::io::CodecType::ZSTD,
                                       compressionLevel)) {
}

ZstdStreamCompressor::~ZstdStreamCompressor() {
}

std::unique_ptr<folly::IOBuf> ZstdStreamCompressor::compress(
    const folly::IOBuf* in, bool last) {
  if (error_) {
    return nullptr;
  }

  if (in == nullptr) {
    error_ = true;
    return nullptr;
  }

  try {
    folly::IOBuf clone;
    if (in->isChained()) {
      clone = in->cloneCoalescedAsValueWithHeadroomTailroom(0, 0);
      in = &clone;
    }

    auto compressBound = codec_->maxCompressedLength(in->length());
    auto out = folly::IOBuf::create(compressBound + 1);

    folly::ByteRange inrange{in->data(), in->length()};
    folly::MutableByteRange outrange{out->writableTail(), out->tailroom()};
    auto op = last ? folly::io::StreamCodec::FlushOp::END
                   : folly::io::StreamCodec::FlushOp::FLUSH;

    auto success = codec_->compressStream(inrange, outrange, op);

    if (!success) {
      error_ = true;
      return {};
    }

    DCHECK_EQ(inrange.size(), 0);
    DCHECK_GT(outrange.size(), 0);

    out->append(outrange.begin() - out->tail());

    return out;
  } catch (const std::exception&) {
    error_ = true;
  }
  return {};
}

} // namespace proxygen
