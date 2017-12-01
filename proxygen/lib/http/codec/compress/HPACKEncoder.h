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

#include <folly/io/IOBuf.h>
#include <list>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <vector>

namespace proxygen {

class HPACKEncoder : public HPACKContext {

 public:
  explicit HPACKEncoder(bool huffman,
                        uint32_t tableSize=HPACK::kTableSize,
                        bool emitSequenceNumbers=false,
                        bool useBaseIndex=false,
                        bool autoCommit=true);

  /**
   * Size of a new IOBuf which is added to the chain
   *
   * jemalloc will round up to 4k - overhead
   */
  static const uint32_t kBufferGrowth = 4000;

  /**
   * Encode the given headers and return the buffer
   */
  virtual std::unique_ptr<folly::IOBuf> encode(
    const std::vector<HPACKHeader>& headers,
    uint32_t headroom = 0,
    bool* eviction = nullptr);

  void setHeaderTableSize(uint32_t size) {
    table_.setCapacity(size);
    pendingContextUpdate_ = true;
  }

  uint32_t getTableSize() const {
    return table_.capacity();
  }

  uint32_t getBytesStored() const {
    return table_.bytes();
  }

  uint32_t getHeadersStored() const {
    return table_.size();
  }

  void setCommitEpoch(uint16_t commitEpoch) {
    if (commitEpoch > commitEpoch_) {
      commitEpoch_ = commitEpoch;
    }
  }

  void packetFlushed() {
    bytesInPacket_ = 0;
    packetEpoch_ = nextSequenceNumber_;
    VLOG(5) << "bumped packetEpoch_ to " << packetEpoch_;
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    indexingStrat_ = indexingStrat;
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return indexingStrat_;
  }

  static void enableAutoFlush() {
    sEnableAutoFlush_ = true;
  }

 protected:
  void encodeAsIndex(uint32_t index);

 private:
  virtual void encodeHeader(const HPACKHeader& header);

  virtual void encodeAsLiteral(const HPACKHeader& header, bool indexable);

  bool huffman_;

  const HeaderIndexingStrategy* indexingStrat_;
 protected:
  HPACKEncodeBuffer buffer_;
  uint16_t packetEpoch_{0};
  uint16_t nextSequenceNumber_{0};
  int32_t commitEpoch_{-1};
  uint16_t bytesInPacket_{0};
  bool pendingContextUpdate_{false};
  bool eviction_{false};
  bool emitSequenceNumbers_{false};
  bool autoCommit_{true};

  static bool sEnableAutoFlush_;
};

}
