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

#include <proxygen/lib/http/codec/compress/QPACKCodec.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionScheme.h>

namespace proxygen { namespace compress {

class QPACKScheme : public CompressionScheme {
 public:
  explicit QPACKScheme(CompressionSimulator* sim, uint32_t tableSize,
                       uint32_t maxBlocking)
      : CompressionScheme(sim) {
    client_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    server_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    client_.setEncoderHeaderTableSize(tableSize);
    server_.setDecoderHeaderTableMaxSize(tableSize);
    client_.setMaxVulnerable(maxBlocking);
    server_.setMaxBlocking(maxBlocking);
  }

  ~QPACKScheme() {
    CHECK_EQ(server_.getQueuedBytes(), 0);
  }

  struct QPACKAck : public CompressionScheme::Ack {
    explicit QPACKAck(uint16_t n, uint16_t cAcks) :
        seqn(n), nControlAcks(cAcks) {
    }
    uint16_t seqn;
    uint16_t nControlAcks;
  };

  std::unique_ptr<Ack> getAck(uint16_t seqn) override {
    VLOG(4) << "Sending ack for seqn=" << seqn;
    auto res = std::make_unique<QPACKAck>(seqn, pendingControlAcks_);
    pendingControlAcks_ = 0;
    return std::move(res);
  }
  void recvAck(std::unique_ptr<Ack> ack) override {
    CHECK(ack);
    auto qpackAck = dynamic_cast<QPACKAck*>(ack.get());
    CHECK_NOTNULL(qpackAck);
    VLOG(4) << "Received ack for seqn=" << qpackAck->seqn
            << " with nControlAcks=" << qpackAck->nControlAcks;
    while (qpackAck->nControlAcks--) {
      client_.onControlHeaderAck();
    }
    client_.onHeaderAck(qpackAck->seqn);
  }

  std::pair<FrameFlags, std::unique_ptr<folly::IOBuf>> encode(
      bool /*newPacket*/,
      std::vector<compress::Header> allHeaders,
      SimStats& stats) override {
    index++;
    auto result = client_.encode(allHeaders, index);
    uint16_t len = 0;
    folly::IOBufQueue queue;
    static const uint32_t growth = 1400; // chosen arbitrarily
    folly::io::QueueAppender cursor(&queue, growth);
    if (result.control) {
      VLOG(5) << "Writing encodeControlIndex_=" << encodeControlIndex_;
      len = result.control->computeChainDataLength();
      cursor.writeBE<uint16_t>(len);
      cursor.writeBE<uint16_t>(encodeControlIndex_++);
      cursor.insert(std::move(result.control));
      // Don't count the framing against the compression ratio, for now
      // stats.compressed += 3 * sizeof(uint16_t);
    } else {
      cursor.writeBE<uint16_t>(0);
    }
    if (result.stream) {
      len = result.stream->computeChainDataLength();
    }
    cursor.writeBE<uint16_t>(index);
    cursor.writeBE<uint16_t>(len);
    cursor.insert(std::move(result.stream));
    stats.uncompressed += client_.getEncodedSize().uncompressed;
    stats.compressed += client_.getEncodedSize().compressed;
    // OOO is allowed if there has not been an eviction
    FrameFlags flags(false, false);
    return {flags, queue.move()};
  }

  void decode(FrameFlags flags,
              std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats& stats,
              SimStreamingCallback& callback) override {
    folly::io::Cursor cursor(encodedReq.get());
    auto toTrim = sizeof(uint16_t) * 3;
    auto len = cursor.readBE<uint16_t>();
    if (len > 0) {
      // check decode result
      auto controlIndex = cursor.readBE<uint16_t>();
      toTrim += sizeof(uint16_t);
      if (controlIndex == decodeControlIndex_) {
        // next expected control block, decode
        VLOG(5) << "decode controlIndex=" << controlIndex;
        server_.decodeControl(cursor, len);
        pendingControlAcks_++;
        decodeControlIndex_++;
        while (!controlQueue_.empty() &&
               controlQueue_.begin()->first == decodeControlIndex_) {
          // drain the queue
          VLOG(5) << "decode controlIndex=" << controlQueue_.begin()->first;
          auto it = controlQueue_.begin();
          folly::io::Cursor controlCursor(it->second.get());
          server_.decodeControl(controlCursor, controlCursor.totalLength());
          pendingControlAcks_++;
          decodeControlIndex_++;
          controlQueue_.erase(it);
        }
      } else {
        // out of order control block, queue it
        std::unique_ptr<folly::IOBuf> control;
        cursor.clone(control, len);
        controlQueue_.emplace(controlIndex, std::move(control));
      }
      toTrim += len;
    }
    auto seqn = cursor.readBE<uint16_t>();
    callback.seqn = seqn;
    VLOG(1) << "Decoding request=" << callback.requestIndex
            << " header seqn=" << seqn
            << " allowOOO=" << uint32_t(flags.allowOOO);
    len = cursor.readBE<uint16_t>();
    folly::IOBufQueue queue;
    queue.append(std::move(encodedReq));
    queue.trimStart(toTrim);
    server_.decodeStreaming(queue.move(), len, &callback);
    callback.maybeMarkHolDelay();
    if (server_.getQueuedBytes() > stats.maxQueueBufferBytes) {
      stats.maxQueueBufferBytes = server_.getQueuedBytes();
    }
  }

  uint32_t getHolBlockCount() const override {
    return server_.getHolBlockCount();
  }

  QPACKCodec client_{TransportDirection::UPSTREAM};
  QPACKCodec server_{TransportDirection::UPSTREAM};
  std::map<uint16_t, std::unique_ptr<folly::IOBuf>> controlQueue_;
  uint16_t encodeControlIndex_{0};
  uint16_t decodeControlIndex_{0};
  uint32_t pendingControlAcks_{0};
  std::deque<uint16_t> acks_;
};

}} // namespace proxygen::compress
