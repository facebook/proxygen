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
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <folly/experimental/Bits.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

class HPACKHeader;

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the NPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class QPACKCodec : public HeaderCodec, public QPACKDecoder::Callback {
 public:
  explicit QPACKCodec();
  ~QPACKCodec() override {}

  using EncodeResult = std::pair<std::unique_ptr<folly::IOBuf>,
                                 std::unique_ptr<folly::IOBuf>>;

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>&) noexcept override {
    LOG(FATAL) << "deprecated: use encodeQuic instead";
    return nullptr;
  }

  EncodeResult encodeQuic(
    std::vector<compress::Header>& headers) noexcept;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

  void decodeControlStream(folly::io::Cursor& cursor,
                           uint32_t totalBytes);
  void decodeStreaming(
      folly::io::Cursor& cursor,
      uint32_t length,
      HeaderCodec::StreamingCallback* streamingCb) noexcept override;

  void describe(std::ostream& os) const;

  void setMaxUncompressed(uint32_t maxUncompressed) override {
    HeaderCodec::setMaxUncompressed(maxUncompressed);
    decoder_.setMaxUncompressed(maxUncompressed);
  }

  std::unique_ptr<folly::IOBuf> moveAcks() {
    return std::move(acks_);
  }

  void applyAcks(const folly::IOBuf* acks) {
    encoder_.deleteAck(acks);
  }

  uint32_t getHolBlockCount() const {
    return holBlockCount_;
  }

  uint32_t getQueuedBytes() const {
    return decoder_.getQueuedBytes();
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    encoder_.setHeaderIndexingStrategy(indexingStrat);
  }

 protected:
  QPACKEncoder encoder_;
  QPACKDecoder decoder_;

 private:
  std::unique_ptr<folly::IOBuf> acks_;
  void ack(uint32_t ackIndex) override {
    // For now we're only using a 16 byte bitvector, so we cannot ack any index
    // greater than 16 * 8 = 256.
    static const size_t kAckBlockSize = 16;
    static const size_t kMaxIndex = kAckBlockSize * 8;
    CHECK_LE(ackIndex, kMaxIndex);
    if (!acks_) {
      acks_ = folly::IOBuf::create(kAckBlockSize);
      memset(acks_->writableData(), 0, kAckBlockSize);
      acks_->append(kAckBlockSize);
    }
    folly::Bits<uint8_t>::set(acks_->writableData(), ackIndex);

  }
  void onError() override {}
  std::vector<HPACKHeader> decodedHeaders_;
  uint32_t holBlockCount_{0};
};

std::ostream& operator<<(std::ostream& os, const QPACKCodec& codec);
}
