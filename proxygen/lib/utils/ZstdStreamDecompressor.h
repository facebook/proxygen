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

// We need access to zstd internals (to read frame headers etc.)
#define ZSTD_STATIC_LINKING_ONLY
#define ZDICT_STATIC_LINKING_ONLY

#include <memory>
#include <zstd.h>
#include <zdict.h>

namespace folly {
class IOBuf;
}

namespace proxygen {

enum class ZstdStatusType: int {
  NONE,
  SUCCESS,
  NODICT,
  CONTINUE,
  ERROR,
 };


class ZstdStreamDecompressor {
 public:
  explicit ZstdStreamDecompressor(size_t, std::string);
  ~ZstdStreamDecompressor();
  std::unique_ptr<folly::IOBuf> decompress(const folly::IOBuf* in);
  ZstdStatusType getStatus() {return status_;};
  ZstdStatusType status_;

 private:
  ZSTD_DStream *dStream_{nullptr};
  ZSTD_DDict* dDict_{nullptr};
  size_t totalLen_{0};
  size_t totalDec_{0};
};
}
