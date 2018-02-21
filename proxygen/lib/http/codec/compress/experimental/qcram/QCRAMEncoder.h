/*
 *  Copyright (c) 2017-present, Facebook, Inc.
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
#include <map>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMContext.h>
#include <set>
#include <unordered_map>
#include <vector>

namespace proxygen {

class QCRAMEncoder : public QCRAMContext {
 public:
  class EncodeResult {
   public:
    // We will model total order on control stream via depends.  So
    // for control stream blockes, depends is just the most recent
    // insertion before the block was encoded.
    mutable std::unique_ptr<folly::IOBuf> controlBuffer;
    // Stream blocks will usual depend on an entry from their control block.
    // Unless there were no insertions.
    mutable std::unique_ptr<folly::IOBuf> streamBuffer;
    uint32_t streamDepends;
  };

  explicit QCRAMEncoder(bool huffman,
                        uint32_t tableSize = HPACK::kTableSize,
                        bool emitSequenceNumbers = false,
                        bool autoCommit = true,
                        bool enableUpdatesOnControlStream = true);

  /**
   * Size of a new IOBuf which is added to the chain
   *
   * jemalloc will round up to 4k - overhead
   */
  static const uint32_t kBufferGrowth = 4000;

  /**
   * Encode the given headers and return the buffer
   */
  EncodeResult encode(bool newPacket,
                      const std::vector<QCRAMHeader>& headers,
                      uint32_t headroom = 0);

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
    if (packetEpoch_ == nextSequenceNumber_) {
      return;
    }
    packetEpoch_ = nextSequenceNumber_;
    if (bytesInPacket_ > 0) {
      VLOG(5) << "encoded packet bytes " << bytesInPacket_;
    }
    VLOG(5) << "bumped packetEpoch_ to " << packetEpoch_;
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    indexingStrat_ = indexingStrat;
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return indexingStrat_;
  }

  uint32_t literalOverhead() const {
    return literalOverhead_;
  }

  uint32_t relativeToAbsoluteIndex(uint32_t index) {
    return table_.toInternal(globalToDynamicIndex(index));
  }

  void addRef(uint32_t index) {
    table_.addRef(globalToDynamicIndex(index));
    headerRefs_.insert(relativeToAbsoluteIndex(index));
  }

  void recvAck(uint16_t seqn);

 protected:
  void encodeAsIndex(uint32_t index);

 private:
  virtual void encodeHeader(const HPACKHeader& header);

  virtual void encodeAsLiteral(const HPACKHeader& header, bool indexable);

  const HeaderIndexingStrategy* indexingStrat_;

 protected:
  HPACKEncodeBuffer buffer_;
  HPACKEncodeBuffer prefix_;
  uint16_t packetEpoch_{0};
  uint16_t nextSequenceNumber_{0};
  int32_t commitEpoch_{-1};
  uint16_t bytesInPacket_{0};
  bool pendingContextUpdate_{false};
  bool emitSequenceNumbers_{false};
  bool autoCommit_{true};
  bool enableUpdatesOnControlStream_{true};

  uint32_t literalOverhead_{0};
  uint32_t baseIndexOverhead_{0};

  static bool sEnableAutoFlush_;

  // If true, allow generating indexed representations referring to
  // vulnerable entries.
  bool allowVulnerable_{true};

  bool controlBlock_;

  // After encoding a block, this will contain the index of the most
  // recent (smallest HPACK) vulnerable entry the block depends on.
  // It will be max() the block doesn't depend on vulnerable entries.
  uint32_t depends_{0};

  // In QCRAM, don't evict entries that have outstanding references.
  // Map from unacked header to referenced entries.  When header is
  // acked, use this to decrement corresponding members of refCounts_.
  std::map<uint16_t, std::set<uint32_t>> outstandingRefs_;
  std::set<uint32_t> headerRefs_;
};

} // namespace proxygen
