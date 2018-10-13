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
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKTableInfo.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

class HPACKHeader;

namespace compress {
std::pair<std::vector<HPACKHeader>, uint32_t> prepareHeaders(
    std::vector<Header>& headers);
}

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the NPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class HPACKCodec : public HeaderCodec {
 public:
  explicit HPACKCodec(TransportDirection direction);
  ~HPACKCodec() override {}

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept;

  void decodeStreaming(
      folly::io::Cursor& cursor,
      uint32_t length,
      HPACK::StreamingCallback* streamingCb) noexcept;

  void setEncoderHeaderTableSize(uint32_t size) {
    encoder_.setHeaderTableSize(size);
  }

  void setDecoderHeaderTableMaxSize(uint32_t size) {
    decoder_.setHeaderTableMaxSize(size);
  }

  void describe(std::ostream& os) const;

  void setMaxUncompressed(uint64_t maxUncompressed) override {
    HeaderCodec::setMaxUncompressed(maxUncompressed);
    decoder_.setMaxUncompressed(maxUncompressed);
  }

  HPACKTableInfo getHPACKTableInfo() const {
    return HPACKTableInfo(encoder_.getTableSize(),
                          encoder_.getBytesStored(),
                          encoder_.getHeadersStored(),
                          decoder_.getTableSize(),
                          decoder_.getBytesStored(),
                          decoder_.getHeadersStored());
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    encoder_.setHeaderIndexingStrategy(indexingStrat);
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return encoder_.getHeaderIndexingStrategy();
  }

 protected:
  HPACKEncoder encoder_;
  HPACKDecoder decoder_;

 private:
  void recordCompressedSize(const folly::IOBuf* buf);

  std::vector<HPACKHeader> decodedHeaders_;
};

std::ostream& operator<<(std::ostream& os, const HPACKCodec& codec);
}
