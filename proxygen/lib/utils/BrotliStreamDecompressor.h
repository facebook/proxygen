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

#include <dec/decode.h>
#include <dec/state.h>
#include <folly/io/IOBuf.h>
#include <memory>

namespace proxygen {

enum class BrotliStatusType: int {
  NONE,
  SUCCESS,
  CONTINUE,
  ERROR,
};

class BrotliStreamDecompressor {
 public:
  explicit BrotliStreamDecompressor();
  ~BrotliStreamDecompressor();

  std::unique_ptr<folly::IOBuf> decompress(const folly::IOBuf* in);
  BrotliStatusType getStatus() { return status_;}
 protected:
  BrotliStatusType status_;

 private:
  BrotliState *state_;
};
}
