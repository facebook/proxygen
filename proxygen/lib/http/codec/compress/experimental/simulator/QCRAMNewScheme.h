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
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMCodec.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMQueue.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>

namespace proxygen {  namespace compress {
class QCRAMNewScheme : public CompressionScheme {
 public:
  explicit QCRAMNewScheme(CompressionSimulator* sim, uint32_t tableSize)
      : CompressionScheme(sim) {
    client_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    server_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    client_.setEncoderHeaderTableSize(tableSize);
    server_.setDecoderHeaderTableMaxSize(tableSize);
  }

  ~QCRAMNewScheme() {
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
    client_.recvAck(qcramAck->seqn);
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
      bool newPacket,
      std::vector<compress::Header> allHeaders,
      SimStats& stats) override {
    index++;
    auto result = client_.encode(newPacket, allHeaders);
    stats.uncompressed += client_.getEncodedSize().uncompressed;
    stats.compressed += client_.getEncodedSize().compressed;
    // TODO(ckrasic) - HPACK doesn't count seqno in compressed size.
    // Doing this makes comparison to HPACK clearer.  Move this into
    // codec so that we can deal with depends also?
    if (result.controlBuffer) {
      stats.compressed -= sizeof(uint16_t);
    }
    if (result.streamBuffer) {
      stats.compressed -= sizeof(uint16_t);
    }
    // OOO is always allowed
    struct FrameFlags flags{true, (result.streamDepends != kMaxIndex) };
    if (sEnableUpdatesOnControlStream_) {
      // QCRAMCodec::encode returns a control buffer and a stream
      // buffer, but the encode API must return a single buffer.  Prefix
      // the control bytes with a length so the decoder can do the right
      // thing.  We'll need to refactor the simulator to handle control
      // streams separately from request streams.
      //
      // The simulator will not decode the control stream bytes until
      // all the packets of this header block are received.  This could cause
      // QPACK to report HoL delays that wouldn't occur in practice.
      auto header = folly::IOBuf::create(4);
      header->append(4);
      folly::io::RWPrivateCursor cursor(header.get());
      auto controlLen = result.controlBuffer ?
          result.controlBuffer->computeChainDataLength() : 0;
      cursor.writeBE<uint32_t>(controlLen);
      VLOG(1) << "Encode with control buffer len " << controlLen;
      if (result.controlBuffer) {
        header->prependChain(std::move(result.controlBuffer));
      }
      if (result.streamBuffer) {
        header->prependChain(std::move(result.streamBuffer));
      }
      return {flags, std::move(header)};
    }
    return {flags, std::move(result.streamBuffer)};
  }

  void decodeControl(std::unique_ptr<folly::IOBuf> buf, SimStats& stats) {
    VLOG(5) << "Decode control buf len " << buf->length();
    folly::io::Cursor cursor(buf.get());
    auto len = cursor.totalLength();
    VLOG(5) << "Decode control cursor len " << len;
    uint32_t depends = kMaxIndex;
    HPACKDecodeBuffer dbuf(cursor, len, HeaderCodec::kMaxUncompressed);
    auto err = dbuf.decodeInteger(8, depends);
    if (err != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Decode error decoding maxSize err_=" << err;
    }
    VLOG(1) << "Decoder decode request prefix depends=" << depends;
    len -= dbuf.consumedBytes();
    cursor.peek();
    buf = buf->pop();
    auto seqn = cursor.readBE<uint16_t>();
    VLOG(1) << "read seqn " << seqn;
    buf->trimStart(sizeof(uint16_t));
    len -= sizeof(uint16_t);
    struct FrameFlags flags{true, (seqn > 0)};
    serverQueue_.enqueueHeaderBlock(
      seqn, std::move(buf), len, nullptr,
      {flags.allowOOO, flags.QCRAMPrefixHasDepends}, depends);
    if (serverQueue_.getQueuedBytes() > stats.maxQueueBufferBytes) {
      stats.maxQueueBufferBytes = serverQueue_.getQueuedBytes();
    }
  }

  void decode(FrameFlags flags, std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats& stats, SimStreamingCallback& callback) override {
    if (sEnableUpdatesOnControlStream_) {
      folly::io::Cursor cursor(encodedReq.get());
      auto controlLen = cursor.readBE<uint32_t>();
      DVLOG(1) << "read control len " << controlLen << " pop "
               << encodedReq->length();
      encodedReq = encodedReq->pop();
      if (controlLen > 0) {
        DCHECK(encodedReq.get() != nullptr);
        auto controlBuffer = encodedReq->pop();
        encodedReq.swap(controlBuffer);
        VLOG(1) << "control buf starts with len " << controlBuffer->length();
        while (controlBuffer->computeChainDataLength() < controlLen) {
          auto buf = encodedReq->pop();
          encodedReq.swap(buf);
          VLOG(1) << "control buf add len " << buf->length();
          controlBuffer->prependChain(std::move(buf));
          VLOG(1) << "control buf chain len "
                  << controlBuffer->computeChainDataLength();
        }
        DCHECK(controlBuffer->computeChainDataLength() == controlLen);
        decodeControl(std::move(controlBuffer), stats);
      }
    }
    folly::io::Cursor cursor(encodedReq.get());
    auto len = cursor.totalLength();
    uint32_t depends = kMaxIndex;
    if (flags.QCRAMPrefixHasDepends) {
      HPACKDecodeBuffer dbuf(cursor, len, HeaderCodec::kMaxUncompressed);
      auto err = dbuf.decodeInteger(8, depends);
      if (err != HPACK::DecodeError::NONE) {
        LOG(ERROR) << "Decode error decoding maxSize err_=" << err;
      }
      VLOG(1) << "Decoder decode request prefix depends=" << depends;
      len -= dbuf.consumedBytes();
    }
    cursor.peek();
    auto seqn = cursor.readBE<uint16_t>();
    callback.seqn = seqn;
    VLOG(1) << "read seqn " << seqn;
    if (flags.QCRAMPrefixHasDepends) {
      encodedReq = encodedReq->pop();
    }
    encodedReq->trimStart(sizeof(uint16_t));
    len -= sizeof(uint16_t);
    VLOG(1) << "Decoding request=" << callback.requestIndex << " header seqn="
            << seqn << " allowOOO=" << uint32_t(flags.allowOOO);
    serverQueue_.enqueueHeaderBlock(
      seqn, std::move(encodedReq), len,
      &callback, {flags.allowOOO, flags.QCRAMPrefixHasDepends}, depends);
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

  static void enableUpdatesOnControlStream() {
    sEnableUpdatesOnControlStream_ = true;
  }

  QCRAMCodec client_{TransportDirection::UPSTREAM, true, false,
      sEnableUpdatesOnControlStream_};
  QCRAMCodec server_{TransportDirection::UPSTREAM, true, false,
      sEnableUpdatesOnControlStream_};
  QCRAMQueue serverQueue_{server_};
  std::deque<uint16_t> acks_;
  int32_t commitEpoch_{-1};
  static bool sEnableUpdatesOnControlStream_;
};
}}
