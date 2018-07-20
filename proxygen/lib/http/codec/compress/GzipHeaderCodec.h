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

#include <folly/Expected.h>
#include <folly/ThreadLocal.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <map>
#include <memory>
#include <proxygen/lib/http/codec/SPDYVersionSettings.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <zlib.h>

namespace proxygen {

enum class GzipDecodeError : uint8_t {
  NONE = 0,
  BAD_ENCODING = 1,
  HEADERS_TOO_LARGE = 2,
  INFLATE_DICTIONARY = 3,
  EMPTY_HEADER_NAME = 4,
  EMPTY_HEADER_VALUE = 5,
  INVALID_HEADER_VALUE = 6
};

class GzipHeaderCodec : public HeaderCodec {

 public:
  GzipHeaderCodec(int compressionLevel,
                  const SPDYVersionSettings& versionSettings);
  explicit GzipHeaderCodec(int compressionLevel,
                           SPDYVersion version = SPDYVersion::SPDY3_1);
  ~GzipHeaderCodec() override;

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept;

  folly::Expected<HeaderDecodeResult, GzipDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept;

  /**
   * same as above, but for decode
   */
  const HTTPHeaderSize& getDecodedSize() {
    return decodedSize_;
  }

 private:
  folly::IOBuf& getHeaderBuf();

  /**
   * Parse the decompressed name/value header block.
   */
  folly::Expected<size_t, GzipDecodeError>
  parseNameValues(const folly::IOBuf& uncompressed,
                  uint32_t uncompressedLength) noexcept;

  const SPDYVersionSettings& versionSettings_;
  z_stream deflater_;
  z_stream inflater_;
  compress::HeaderPieceList outHeaders_;
  HTTPHeaderSize decodedSize_;
};
}
