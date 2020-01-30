/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <proxygen/lib/utils/StreamCompressor.h>

namespace folly {
class IOBuf;
namespace io {
class StreamCodec;
}
} // namespace folly

namespace proxygen {

class ZstdStreamCompressor : public StreamCompressor {
 public:
  explicit ZstdStreamCompressor(int compressionLevel);

  virtual ~ZstdStreamCompressor() override;

  virtual std::unique_ptr<folly::IOBuf> compress(const folly::IOBuf*,
                                                 bool last = true) override;

  virtual bool hasError() override {
    return error_;
  }

 private:
  std::unique_ptr<folly::io::StreamCodec> codec_;
  bool error_ = false;
};
} // namespace proxygen
