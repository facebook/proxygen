/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <zlib.h>
#include <folly/portability/GFlags.h>

#include <proxygen/lib/utils/StreamDecompressor.h>

namespace folly {
class IOBuf;
}

DECLARE_int64(zlib_decompresser_buffer_growth);
DECLARE_int64(zlib_decompresser_buffer_minsize);

namespace proxygen {

/**
 * These are misleading. See zlib.h for explanation of windowBits param. 31
 * implies a window log of 15 with enabled detection and decoding of the gzip
 * format.
 */
constexpr int GZIP_WINDOW_BITS = 31;
constexpr int DEFLATE_WINDOW_BITS = 15;

class ZlibStreamDecompressor : public StreamDecompressor {
 public:
  explicit ZlibStreamDecompressor(CompressionType type);

  ZlibStreamDecompressor() { }

  ~ZlibStreamDecompressor();

  void init(CompressionType type);

  std::unique_ptr<folly::IOBuf> decompress(const folly::IOBuf* in) override;

  int getStatus() { return status_; }

  bool hasError() override {
    return status_ != Z_OK && status_ != Z_STREAM_END;
  }

  bool finished() override {
    return status_ == Z_STREAM_END;
  }

 private:
  CompressionType type_{CompressionType::NONE};
  z_stream zlibStream_;
  int status_{-1};
};
}
