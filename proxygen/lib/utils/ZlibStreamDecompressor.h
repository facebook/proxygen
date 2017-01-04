/*
 *  Copyright (c) 2017, Facebook, Inc.
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

namespace folly {
class IOBuf;
}

DECLARE_int64(zlib_decompresser_buffer_growth);
DECLARE_int64(zlib_decompresser_buffer_minsize);

namespace proxygen {

enum class ZlibCompressionType: int {
  NONE = 0,
  DEFLATE = 15,
  GZIP = 31
};

class ZlibStreamDecompressor {
 public:
  explicit ZlibStreamDecompressor(ZlibCompressionType type);

  ZlibStreamDecompressor() { }

  ~ZlibStreamDecompressor();

  void init(ZlibCompressionType type);

  std::unique_ptr<folly::IOBuf> decompress(const folly::IOBuf* in);

  int getStatus() { return status_; }

  bool hasError() { return status_ != Z_OK && status_ != Z_STREAM_END; }

  bool finished() { return status_ == Z_STREAM_END; }

 private:
  ZlibCompressionType type_{ZlibCompressionType::NONE};
  z_stream zlibStream_;
  int status_{-1};
};


}
