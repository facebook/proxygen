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

#include <folly/io/async/EventBase.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/SimStreamingCallback.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionTypes.h>

namespace proxygen { namespace compress {

class CompressionSimulator;

class CompressionScheme : public folly::EventBase::LoopCallback {
 public:
  explicit CompressionScheme(CompressionSimulator* sim)
      : simulator_(sim) {}
  virtual ~CompressionScheme() {}

  /* Parent class for acks */
  struct Ack { virtual ~Ack() {} };

  /* Generate an ack for the given sequence number */
  virtual std::unique_ptr<Ack> getAck(uint16_t seqn) = 0;

  /* Deliver an ack to the client/encoder */
  virtual void recvAck(std::unique_ptr<Ack>) = 0;

  /* Encode the header list.
   * The simulator sets newPacket if this block should be considered
   * to start a new packet because of a time gap since the previous.
   * Returns a pair { must-process-in-order, header block }
   */
  virtual std::pair<FrameFlags, std::unique_ptr<folly::IOBuf>> encode(
      bool newPacket,
      std::vector<compress::Header> allHeaders,
      SimStats& stats) = 0;

  /* Decode the supplied buffer.  allowOOO indicates if the server can process
   * out of order.
   */
  virtual void decode(FrameFlags flags, std::unique_ptr<folly::IOBuf> encodedReq,
                      SimStats& stats, SimStreamingCallback& cb) = 0;

  /* Return the number of times the decoder was head-of-line blocked */
  virtual uint32_t getHolBlockCount() const = 0;

  /* Loop callback simulates packet flushing once per loop*/
  void runLoopCallback() noexcept override;

  /* List of blocks encoded in the current event loop */
std::list<std::tuple<FrameFlags, bool /*newPacket*/, std::unique_ptr<folly::IOBuf>,
                       SimStreamingCallback*>> encodedBlocks;

std::list<std::tuple<FrameFlags, bool /*newPacket*/, std::unique_ptr<folly::IOBuf>,
                       SimStreamingCallback*>> packetBlocks;

  // Running index of how many requests have been compressed with this scheme
  size_t index{0};

  // Used for starting new packets.
  std::chrono::milliseconds prev;

  size_t packetBytes{0};
  std::chrono::milliseconds decodeDelay;

  std::list<uint16_t> packetIndices;


 private:
  CompressionSimulator* simulator_;
};

}}
