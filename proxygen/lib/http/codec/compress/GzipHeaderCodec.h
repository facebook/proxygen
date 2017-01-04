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

#include <folly/ThreadLocal.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <map>
#include <memory>
#include <proxygen/lib/http/codec/SPDYVersionSettings.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <zlib.h>

namespace proxygen {

class GzipHeaderCodec : public HeaderCodec {

 public:
  GzipHeaderCodec(int compressionLevel,
                  const SPDYVersionSettings& versionSettings);
  explicit GzipHeaderCodec(int compressionLevel,
                           SPDYVersion version = SPDYVersion::SPDY3_1);
  ~GzipHeaderCodec() override;

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept override;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

  void decodeStreaming(
      folly::io::Cursor& cursor,
      uint32_t length,
      HeaderCodec::StreamingCallback* streamingCb) noexcept override;

 private:
  folly::IOBuf& getHeaderBuf();

  /**
   * Parse the decompressed name/value header block.
   */
  Result<size_t, HeaderDecodeError>
  parseNameValues(const folly::IOBuf& uncompressed,
                  uint32_t uncompressedLength) noexcept;

  const SPDYVersionSettings& versionSettings_;
  z_stream deflater_;
  z_stream inflater_;
};
}
