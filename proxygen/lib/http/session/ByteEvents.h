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

#include <folly/IntrusiveList.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <proxygen/lib/utils/Time.h>

namespace proxygen {

class ByteEvent {
 public:
  enum EventType {
    FIRST_BYTE,
    LAST_BYTE,
    PING_REPLY_SENT,
    FIRST_HEADER_BYTE,
    TRACKED_BYTE,
  };

  ByteEvent(uint64_t byteOffset, EventType eventType)
      : eventType_(eventType), bufferWriteTracked_(0), byteOffset_(byteOffset) {
  }
  virtual ~ByteEvent() {
  }
  virtual HTTPTransaction* getTransaction() {
    return nullptr;
  }
  virtual int64_t getLatency() {
    return -1;
  }

  folly::SafeIntrusiveListHook listHook;
  EventType eventType_ : 3; // packed w/ byteOffset_
  // bufferWriteTracked_ is used by TX and ACK-tracking ByteEventTrackers to
  // mark that timestamping has been requested for this ByteEvent. TX timestamps
  // are requested for FIRST_BYTE and LAST_BYTE events, ACK timestamps are
  // requested only for LAST_BYTE events.
  //
  // for ByteEvents with timestamps requested, TX and ACK timestamps can be
  // captured by the ByteEventTracker::Callback handler by requesting them
  // via calls to addTxByteEvent and addAckByteEvent respectively when the
  // handler is processing callbacks for onFirstByteEvent and onLastByteEvent.
  size_t bufferWriteTracked_ : 1; // packed w/ byteOffset_
  uint64_t byteOffset_ : (8 * sizeof(uint64_t) - 4);
};

std::ostream& operator<<(std::ostream& os, const ByteEvent& txn);

class TransactionByteEvent : public ByteEvent {
 public:
  TransactionByteEvent(uint64_t byteNo,
                       EventType eventType,
                       HTTPTransaction* txn)
      : ByteEvent(byteNo, eventType), txn_(txn) {
    txn_->incrementPendingByteEvents();
  }

  ~TransactionByteEvent() {
    txn_->decrementPendingByteEvents();
  }

  HTTPTransaction* getTransaction() override {
    return txn_;
  }

  HTTPTransaction* txn_;
};

/**
 * TimestampByteEvents are used to wait for TX and ACK timestamps.
 *
 * Contain a timeout that determines when the timestamp event expires (e.g., we
 * stop waiting to receive the timestamp from the system).
 */
class TimestampByteEvent
    : public TransactionByteEvent
    , public AsyncTimeoutSet::Callback {
 public:
  enum TimestampType {
    TX,
    ACK,
  };
  /**
   * The instances of TimestampByteEvent::Callback *MUST* outlive the ByteEvent
   * it is registered on.
   */
  class Callback {
   public:
    virtual ~Callback() {
    }
    virtual void timeoutExpired(TimestampByteEvent* event) noexcept = 0;
  };

  TimestampByteEvent(TimestampByteEvent::Callback* callback,
                     TimestampType timestampType,
                     uint64_t byteNo,
                     EventType eventType,
                     HTTPTransaction* txn)
      : TransactionByteEvent(byteNo, eventType, txn),
        timestampType_(timestampType),
        callback_(callback) {
  }

  void timeoutExpired() noexcept override {
    callback_->timeoutExpired(this);
  }

  const TimestampType timestampType_;

 private:
  Callback* callback_;
};

class PingByteEvent : public ByteEvent {
 public:
  PingByteEvent(uint64_t byteOffset, TimePoint pingRequestReceivedTime)
      : ByteEvent(byteOffset, PING_REPLY_SENT),
        pingRequestReceivedTime_(pingRequestReceivedTime) {
  }

  int64_t getLatency() override;

  TimePoint pingRequestReceivedTime_;
};

} // namespace proxygen
