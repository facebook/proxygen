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

#include <folly/portability/GFlags.h>
#include <memory>
#include <proxygen/lib/utils/StreamCompressor.h>
#include <proxygen/lib/utils/ZlibStreamDecompressor.h>
#include <zlib.h>

namespace folly {
class IOBuf;
}

DECLARE_int64(zlib_compressor_buffer_growth);

namespace proxygen {

class ZlibStreamCompressor : public StreamCompressor {
 public:
  explicit ZlibStreamCompressor(CompressionType type, int level);

  ~ZlibStreamCompressor();

  void init(CompressionType type, int level);

  std::unique_ptr<folly::IOBuf> compress(const folly::IOBuf* in,
                                         bool trailer = true) override;

  int getStatus() { return status_; }

  bool hasError() override {
    return status_ != Z_OK && status_ != Z_STREAM_END;
  }

  bool finished() { return status_ == Z_STREAM_END; }

 private:
  CompressionType type_{CompressionType::NONE};
  int level_{Z_DEFAULT_COMPRESSION};
  z_stream zlibStream_;
  int status_{-1};
};
}
