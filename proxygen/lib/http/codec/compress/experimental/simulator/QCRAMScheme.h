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

#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionScheme.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKQueue.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>

namespace proxygen {  namespace compress {
class QCRAMScheme : public CompressionScheme {
 public:
  explicit QCRAMScheme(CompressionSimulator* sim)
      : CompressionScheme(sim) {
    client_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    server_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
  }

  ~QCRAMScheme() {
    CHECK_EQ(serverQueue_.getQueuedBytes(), 0);
  }

  struct QCRAMAck : public CompressionScheme::Ack {
    explicit QCRAMAck(uint16_t n) : seqn(n) {}
    uint16_t seqn;
  };

  std::unique_ptr<Ack> getAck(uint16_t seqn) override {
    VLOG(4) << "Sending ack for seqn=" << seqn;
    return std::make_unique<QCRAMAck>(seqn);
  }
  void recvAck(std::unique_ptr<Ack> ack) override {
    CHECK(ack);
    auto qcramAck = dynamic_cast<QCRAMAck*>(ack.get());
    CHECK_NOTNULL(qcramAck);
    VLOG(4) << "Received ack for seqn=" << qcramAck->seqn;
    // acks can arrive out of order.  Only set the commit epoch for the highest
    // sequential ack.
    if (qcramAck->seqn == commitEpoch_ + 1) {
      commitEpoch_ = qcramAck->seqn;
      while (!acks_.empty() && acks_.front() == commitEpoch_ + 1) {
        commitEpoch_ = acks_.front();
        acks_.pop_front();
      }
      client_.setCommitEpoch(commitEpoch_);
    } else {
      acks_.insert(std::lower_bound(acks_.begin(), acks_.end(), qcramAck->seqn),
                   qcramAck->seqn);
    }
  }

  std::pair<FrameFlags, std::unique_ptr<folly::IOBuf>> encode(
      bool /*newPacket*/,
      std::vector<compress::Header> allHeaders, SimStats& stats) override {
    index++;
    bool eviction = false;
    auto block = client_.encode(allHeaders, eviction);
    stats.uncompressed += client_.getEncodedSize().uncompressed;
    stats.compressed += client_.getEncodedSize().compressed;
    // OOO is allowed if there has not been an eviction
    FrameFlags flags;
    flags.allowOOO = !eviction;
    return {flags, std::move(block)};
  }

  void decode(FrameFlags flags, std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats& stats, SimStreamingCallback& callback) override {
    folly::io::Cursor cursor(encodedReq.get());
    auto seqn = cursor.readBE<uint16_t>();
    callback.seqn = seqn;
    VLOG(1) << "Decoding request=" << callback.requestIndex << " header seqn="
            << seqn << " allowOOO=" << uint32_t(flags.allowOOO);
    auto len = cursor.totalLength();
    encodedReq->trimStart(sizeof(uint16_t));
    serverQueue_.enqueueHeaderBlock(seqn, std::move(encodedReq), len,
                                    &callback, flags.allowOOO);
    callback.maybeMarkHolDelay();
    if (serverQueue_.getQueuedBytes() > stats.maxQueueBufferBytes) {
      stats.maxQueueBufferBytes = serverQueue_.getQueuedBytes();
    }
  }

  uint32_t getHolBlockCount() const override {
    return serverQueue_.getHolBlockCount();
  }

  void runLoopCallback() noexcept override {
    CompressionScheme::runLoopCallback();
    // Resets packetEpoch to nextSequenceNumber in the encoder so it can't
    // compress against headers already sent.
    client_.packetFlushed();
  }

  HPACKCodec client_{TransportDirection::UPSTREAM, true, true, false};
  HPACKCodec server_{TransportDirection::UPSTREAM, true, true, false};
  HPACKQueue serverQueue_{server_};
  std::deque<uint16_t> acks_;
  int32_t commitEpoch_{-1};
};
}}
