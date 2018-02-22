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

#include <folly/io/async/DestructorCheck.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>

#include <deque>
#include <memory>
#include <tuple>

namespace proxygen {

class Blocked {
 public:
  Blocked(uint32_t s,
          FrameFlags f,
          std::unique_ptr<folly::IOBuf> b,
          size_t l,
          HeaderCodec::StreamingCallback* cb)
      : seqn(s), flags(f), block(std::move(b)), length(l), streamingCb(cb) {}

  uint32_t seqn;
  FrameFlags flags;
  mutable std::unique_ptr<folly::IOBuf> block;
  size_t length;
  HeaderCodec::StreamingCallback* streamingCb;
};

class QCRAMQueue : public folly::DestructorCheck {
 public:
  explicit QCRAMQueue(QCRAMCodec& codec) : codec_(codec) {
  }

  void enqueueHeaderBlock(uint32_t seqn,
                          std::unique_ptr<folly::IOBuf> block,
                          size_t length,
                          HeaderCodec::StreamingCallback* streamingCb,
                          FrameFlags flags,
                          uint32_t depends) {
    if (!flags.QCRAMPrefixHasDepends || codec_.dependsOK(depends)) {
      // common case, decode immediately
      if (decodeBlock(seqn,
                      flags,
                      std::move(block),
                      length,
                      streamingCb,
                      false /* in order */)) {
        return;
      }
      drainQueue();
      return;
    }
    VLOG(1) << "HoL blocked for seqn " << seqn << " depends " << depends
            << " length " << length;
    queuedBytes_ += length;
    holBlockCount_++;
    Blocked blocked{seqn, flags, std::move(block), length, streamingCb};
    blocked_.emplace(depends, std::move(blocked));
  }

  uint64_t getHolBlockCount() const {
    return holBlockCount_;
  }

  uint64_t getQueuedBytes() const {
    return queuedBytes_;
  }

 private:
  // Returns true if this object was destroyed by its callback.  Callers
  // should check the result and immediately return.
  bool decodeBlock(int32_t seqn,
                   FrameFlags flags,
                   std::unique_ptr<folly::IOBuf> block,
                   size_t length,
                   HeaderCodec::StreamingCallback* cb,
                   bool /*ooo*/) {
    // TODO(ckrasic) - this isn't needed for depends, but might be for
    // fill and/or evicts.
    codec_.setDecoderFrameFlags(flags);
    if (length > 0) {
      VLOG(5) << "decodeBlock for block=" << seqn << " len=" << length;
      folly::io::Cursor c(block.get());
      folly::DestructorCheck::Safety safety(*this);
      codec_.setDecoderFrameFlags(flags);
      codec_.decodeStreaming(c, length, cb);
      if (safety.destroyed()) {
        return true;
      }
    }
    return false;
  }

  void drainQueue() {
    if (blocked_.empty()) {
      return;
    }
    auto it = blocked_.begin();
    while (it != blocked_.end()) {
      uint32_t depends = it->first;
      auto& blocked = it->second;
      if (!codec_.dependsOK(depends)) {
        VLOG(1) << "Still HoL blocked seqn " << blocked.seqn << " depends "
                << depends;
        return;
      }
      VLOG(1) << "draining blocked size " << blocked_.size();
      VLOG(1) << "HoL unblocked seqn " << blocked.seqn << " depends " << depends
              << " length " << blocked.length;
      DCHECK_LE(blocked.length, queuedBytes_);
      queuedBytes_ -= blocked.length;
      decodeBlock(blocked.seqn,
                  blocked.flags,
                  std::move(blocked.block),
                  blocked.length,
                  blocked.streamingCb,
                  false);
      it = blocked_.erase(it);
    }
    VLOG(1) << "HoL queue fully drained";
  }

  uint64_t holBlockCount_{0};
  uint64_t queuedBytes_{0};
  std::multimap<uint32_t, Blocked> blocked_;
  QCRAMCodec& codec_;
};

} // namespace proxygen
