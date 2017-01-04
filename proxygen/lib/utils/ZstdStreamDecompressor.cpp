/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "ZstdStreamDecompressor.h"

#include <folly/Range.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

using folly::IOBuf;
using std::unique_ptr;
using namespace proxygen;

ZstdStreamDecompressor::ZstdStreamDecompressor(size_t totalLen, std::string dictStr)
  : totalLen_(totalLen) {
  dStream_ = ZSTD_createDStream();
  if (dictStr != "") {
    dDict_ = ZSTD_createDDict(&dictStr, dictStr.length());
    if (dStream_ == nullptr || dDict_ == nullptr ||
        ZSTD_isError(ZSTD_initDStream_usingDDict(dStream_, dDict_))) {
      status_ = ZstdStatusType::ERROR;
    }
  } else {
    if (dStream_ == nullptr ||
        ZSTD_isError(ZSTD_initDStream(dStream_))) {
      status_ = ZstdStatusType::ERROR;
    }
  }
}

ZstdStreamDecompressor::~ZstdStreamDecompressor() {
  if (dStream_) {
    ZSTD_freeDStream(dStream_);
  }
  if (dDict_) {
    ZSTD_freeDDict(dDict_);
  }
}

std::unique_ptr<folly::IOBuf> ZstdStreamDecompressor::decompress(
  const folly::IOBuf* in) {
  if (dStream_ == nullptr) {
    status_ = ZstdStatusType::ERROR;
    return nullptr;
  }

  auto out = folly::IOBuf::create(ZSTD_DStreamOutSize());

  size_t buffOutSize = ZSTD_DStreamOutSize();
  std::unique_ptr<unsigned char[]> buffOut(new unsigned char[buffOutSize]);
  auto appender = folly::io::Appender(out.get(), buffOutSize);

  for (const folly::ByteRange range : *in) {
    ZSTD_inBuffer input = {range.data(), range.size(), 0};
    while (input.pos < input.size) {
      ZSTD_outBuffer output = {buffOut.get(), buffOutSize, 0};
      size_t toRead = ZSTD_decompressStream(dStream_, &output, &input);

      if (ZSTD_isError(toRead)) {
        status_ = ZstdStatusType::ERROR;
        return nullptr;
      }

      if (toRead == 0) {
        ZSTD_resetDStream(dStream_);
      }

      if (output.pos > 0) {
        size_t copied =
          appender.pushAtMost((const uint8_t*)output.dst, output.pos);
        CHECK(copied == output.pos);
      }
      totalDec_ += input.size;

      if (totalDec_ < totalLen_) {
        status_ = ZstdStatusType::CONTINUE;
      } else if (totalDec_ > totalLen_) {
        status_ = ZstdStatusType::ERROR;
      } else {
        status_ = ZstdStatusType::SUCCESS;
      }
    }
  }

  return out;
}
