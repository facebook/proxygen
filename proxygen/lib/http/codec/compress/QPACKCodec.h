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
#include <proxygen/lib/http/codec/compress/HPACKCodec.h> // table info
#include <proxygen/lib/http/codec/compress/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/QPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

class HPACKHeader;

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the ALPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class QPACKCodec : public HeaderCodec {
 public:
  explicit QPACKCodec(TransportDirection direction);
  ~QPACKCodec() override {}

  // QPACK encode: id is used for internal tracking of references
  QPACKEncoder::EncodeResult encode(
    std::vector<compress::Header>& headers, uint64_t id) noexcept;

  HPACK::DecodeError decodeControl(folly::io::Cursor& cursor,
                                   uint32_t totalBytes) {
    // stats?
    return decoder_.decodeControl(cursor, totalBytes);
  }

  // QPACK blocking decode.  The decoder may queue the block if there are
  // unsatisfied dependencies
  void decodeStreaming(
    std::unique_ptr<folly::IOBuf> block,
    uint32_t length,
    HPACK::StreamingCallback* streamingCb) noexcept;

  void setEncoderHeaderTableSize(uint32_t size) {
    encoder_.setHeaderTableSize(size);
  }

  void setDecoderHeaderTableMaxSize(uint32_t size) {
    decoder_.setHeaderTableMaxSize(size);
  }

  // QPACK HEADERS_ACK on the control stream
  void onControlHeaderAck() {
    encoder_.onControlHeaderAck();
  }

  // QPACK HEADERS_ACK for a request stream
  void onHeaderAck(uint64_t streamId) {
    encoder_.onHeaderAck(streamId, false);
  }

  // QPACK when a stream is reset.  Clears all reference counts for outstanding
  // blocks
  void onStreamReset(uint64_t streamId) {
    encoder_.onHeaderAck(streamId, true);
  }

  void describe(std::ostream& os) const;

  void setMaxUncompressed(uint32_t maxUncompressed) override {
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

  uint64_t getHolBlockCount() const {
    return decoder_.getHolBlockCount();
  }

  uint64_t getQueuedBytes() const {
    return decoder_.getQueuedBytes();
  }

  void setMaxVulnerable(uint32_t maxVulnerable) {
    encoder_.setMaxVulnerable(maxVulnerable);
  }

  void setMaxBlocking(uint32_t maxBlocking) {
    decoder_.setMaxBlocking(maxBlocking);
  }

 protected:
  QPACKEncoder encoder_;
  QPACKDecoder decoder_;

 private:
  void recordCompressedSize(const QPACKEncoder::EncodeResult& encodeRes);

  std::vector<HPACKHeader> decodedHeaders_;
};

std::ostream& operator<<(std::ostream& os, const QPACKCodec& codec);
}
