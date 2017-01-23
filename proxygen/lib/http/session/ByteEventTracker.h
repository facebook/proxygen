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

#include <proxygen/lib/http/session/AckLatencyEvent.h>
#include <proxygen/lib/http/session/ByteEvents.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/Time.h>

namespace proxygen {

class TTLBAStats;

// An API for using TCP events from the HTTP level
class ByteEventTracker {
 public:
  class Callback {
   public:
    virtual ~Callback() {}
    virtual void onPingReplyLatency(int64_t latency) noexcept = 0;
    virtual uint64_t getAppBytesWritten() noexcept = 0;
    virtual uint64_t getRawBytesWritten() noexcept = 0;
    virtual void onDeleteAckEvent() = 0;
  };

  virtual ~ByteEventTracker();
  explicit ByteEventTracker(Callback* callback): callback_(callback) {}

  void absorb(ByteEventTracker&& other);
  void setCallback(Callback* callback) { callback_ = callback; }

  void addPingByteEvent(size_t pingSize,
                        TimePoint timestamp,
                        uint64_t bytesScheduled);

  void addFirstBodyByteEvent(uint64_t offset, HTTPTransaction* txn);
  void addFirstHeaderByteEvent(uint64_t offset, HTTPTransaction* txn);

  virtual size_t drainByteEvents();
  virtual bool processByteEvents(std::shared_ptr<ByteEventTracker> self,
                                 uint64_t bytesWritten,
                                 bool eorTrackingEnabled);
  virtual void addLastByteEvent(HTTPTransaction* txn,
                                uint64_t byteNo,
                                bool eorTrackingEnabled) noexcept;

  /**
   * Returns the number of bytes needed or 0 when there's nothing to do.
   */
  virtual uint64_t preSend(bool* cork, bool* eom, uint64_t bytesWritten);

  virtual void addAckToLastByteEvent(
      HTTPTransaction* /* txn */,
      const ByteEvent& /* lastByteEvent */,
      bool /* eorTrackingEnabled */) {}

  virtual void deleteAckEvent(
    std::vector<AckByteEvent*>::iterator& /* it */) noexcept {}

  virtual bool setMaxTcpAckTracked(
    uint32_t /* maxAckTracked */,
    AsyncTimeoutSet* /* ackLatencyTimeouts */,
    folly::AsyncTransportWrapper* /* transport */) { return false; }

  virtual void setTTLBAStats(TTLBAStats* /* stats */) {}

  virtual void onAckLatencyEvent(const AckLatencyEvent&) {}

 private:
  // byteEvents_ is in the ascending order of ByteEvent::byteOffset_
  folly::IntrusiveList<ByteEvent, &ByteEvent::listHook> byteEvents_;

 protected:
  Callback* callback_;

  ByteEvent* nextLastByteEvent_{nullptr};
};

} // proxygen
