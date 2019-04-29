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

#include <proxygen/lib/http/session/AckLatencyEvent.h>
#include <proxygen/lib/http/session/ByteEvents.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/Time.h>

namespace proxygen {

class TTLBAStats;

/**
 * ByteEventTracker can be used to fire application callbacks when a given
 * byte of a transport stream has been processed.  The primary usage is to
 * fire the callbacks when the byte is accepted by the transport, not when
 * the byte has been written on the wire, or acknowledged.
 *
 * Subclasses may implement handling of acknowledgement timing.
 */
class ByteEventTracker {
 public:
  class Callback {
   public:
    virtual ~Callback() {}
    virtual void onPingReplyLatency(int64_t latency) noexcept = 0;
    virtual void onFirstByteEvent(HTTPTransaction* txn,
                                  uint64_t offset,
                                  bool bufferWriteTracked) noexcept = 0;
    virtual void onLastByteEvent(HTTPTransaction* txn,
                                 uint64_t offset,
                                 bool bufferWriteTracked) noexcept = 0;
    virtual void onDeleteTxnByteEvent() noexcept = 0;
  };

  virtual ~ByteEventTracker();
  explicit ByteEventTracker(Callback* callback): callback_(callback) {}

  /**
   * Assumes the byte events of another ByteEventTracker that this object
   * is replacing.
   */
  virtual void absorb(ByteEventTracker&& other);
  void setCallback(Callback* callback) { callback_ = callback; }

  /**
   * drainByteEvents should be called to clear out any pending events holding
   * transactions when processByteEvents will no longer be called
   */
  virtual size_t drainByteEvents();

  /**
   * processByteEvents is called whenever the transport has accepted more bytes.
   * bytesWritten is the number of bytes written to the transport over its
   * lifetime.
   */
  virtual bool processByteEvents(std::shared_ptr<ByteEventTracker> self,
                                 uint64_t bytesWritten);

  /**
   * The following methods add byte events for tracking
   */
  void addPingByteEvent(size_t pingSize,
                        TimePoint timestamp,
                        uint64_t bytesScheduled);

  virtual void addFirstBodyByteEvent(uint64_t offset,
                                     HTTPTransaction* txn);

  virtual void addFirstHeaderByteEvent(uint64_t offset, HTTPTransaction* txn);

  virtual void addLastByteEvent(HTTPTransaction* txn, uint64_t byteNo) noexcept;
  virtual void addTrackedByteEvent(HTTPTransaction* txn,
                                   uint64_t byteNo) noexcept;

  /** The base ByteEventTracker cannot track NIC TX. */
  virtual void addTxByteEvent(uint64_t /*offset*/,
                              ByteEvent::EventType /*eventType*/,
                              HTTPTransaction* /*txn*/) {
  }

  /** The base ByteEventTracker cannot track ACKs. */
  virtual void addAckByteEvent(uint64_t /*offset*/, HTTPTransaction* /*txn*/) {
  }

  /**
   * HTTPSession uses preSend to truncate writes on an som or eom boundary.
   *
   * In TX and ACK-tracking ByteEventTrackers, this should examine pending
   * byte events and return the number of bytes until the next first or last
   * byte event, or 0 if none are pending.  If non-zero is returned
   * then som and/or eom may be set to indicate that the buffer contains the
   * start and/or end of a message so that relevant timestamping can be enabled.
   *
   */
  virtual uint64_t preSend(bool* /*cork*/, bool* /*som*/, bool* /*eom*/,
                           uint64_t /*bytesWritten*/) {
    return 0;
  }

  virtual void setTTLBAStats(TTLBAStats* /* stats */) {}

 protected:
  // byteEvents_ is in the ascending order of ByteEvent::byteOffset_
  folly::CountedIntrusiveList<ByteEvent, &ByteEvent::listHook> byteEvents_;

  /**
   * Called when a FIRST_BYTE event is processed (som = start of message).
   *
   * Used for TX and ACK-tracking ByteEventTrackers to update cached position of
   * the next FIRST_BYTE event.
   */
  virtual void somEventProcessed() {}

  /**
   * Called when a LAST_BYTE event is processed (eom = end of message).
   *
   * Used for TX and ACK-tracking ByteEventTrackers to update cached position of
   * the next LAST_BYTE event.
   */
  virtual void eomEventProcessed() {}

  Callback* callback_;
};

} // proxygen
