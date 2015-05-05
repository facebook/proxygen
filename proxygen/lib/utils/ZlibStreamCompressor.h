/*
 *  Copyright (c) 2015, Facebook, Inc.
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
#include <proxygen/lib/utils/ZlibStreamDecompressor.h>

namespace folly {
class IOBuf;
}

#ifndef NO_LIB_GFLAGS
  #include <gflags/gflags.h>
  DECLARE_int64(zlib_buffer_growth);
  DECLARE_int64(zlib_buffer_minsize);
#endif

namespace proxygen {

#ifdef NO_LIB_GFLAGS
  extern int64_t FLAGS_zlib_buffer_growth;
  extern int64_t FLAGS_zlib_buffer_minsize;
#endif

class ZlibStreamCompressor {
 public:
  explicit ZlibStreamCompressor(ZlibCompressionType type, int level);

  ~ZlibStreamCompressor();

  void init(ZlibCompressionType type, int level);

  std::unique_ptr<folly::IOBuf> compress(const folly::IOBuf* in,
                                         bool trailer = true);

  int getStatus() { return status_; }

  bool hasError() { return status_ != Z_OK && status_ != Z_STREAM_END; }

  bool finished() { return status_ == Z_STREAM_END; }

 private:
  ZlibCompressionType type_{ZlibCompressionType::NONE};
  int level_{Z_DEFAULT_COMPRESSION};
  z_stream zlibStream_;
  int status_{-1};
};
}
