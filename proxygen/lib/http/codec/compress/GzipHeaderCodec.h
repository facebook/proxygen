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
  virtual ~GzipHeaderCodec();

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept override;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

 private:

  folly::IOBuf& getHeaderBuf();

  /**
   * Parse the decompressed name/value header block.
   */
  Result<size_t, HeaderDecodeError>
  parseNameValues(const folly::IOBuf&) noexcept;

  const SPDYVersionSettings& versionSettings_;
  z_stream deflater_;
  z_stream inflater_;

  // Pre-initialized compression contexts seeded with the
  // starting dictionary for different SPDY versions - cloning
  // one of these is faster than initializing and seeding a
  // brand new deflate context.
  struct ZlibConfig {

    ZlibConfig(SPDYVersion inVersion, int inCompressionLevel)
        : version(inVersion), compressionLevel(inCompressionLevel) {}

    bool operator==(const ZlibConfig& lhs) const {
      return (version == lhs.version) &&
        (compressionLevel == lhs.compressionLevel);
    }

    bool operator<(const ZlibConfig& lhs) const {
      return (version < lhs.version) ||
          ((version == lhs.version) &&
           (compressionLevel < lhs.compressionLevel));
    }
    SPDYVersion version;
    int compressionLevel;
  };

  struct ZlibContext {
    ~ZlibContext() {
      deflateEnd(&deflater);
      inflateEnd(&inflater);
    }

    z_stream deflater;
    z_stream inflater;
  };

  /**
   * get the thread local cached zlib context
   */
  static const ZlibContext* getZlibContext(SPDYVersionSettings versionSettings,
                                           int compressionLevel);

  typedef std::map<ZlibConfig, std::unique_ptr<ZlibContext>> ZlibContextMap;
};

}
