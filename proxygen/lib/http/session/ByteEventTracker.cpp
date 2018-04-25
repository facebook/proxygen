/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/ByteEventTracker.h>

#include <folly/io/async/DelayedDestruction.h>
#include <string>

using std::string;
using std::vector;

namespace proxygen {

ByteEventTracker::~ByteEventTracker() {
  drainByteEvents();
}

void ByteEventTracker::absorb(ByteEventTracker&& other) {
  byteEvents_ = std::move(other.byteEvents_);
}

// The purpose of self is to represent shared ownership during
// processByteEvents.  This allows the owner to release ownership of the tracker
// from a callback without causing problems
bool ByteEventTracker::processByteEvents(std::shared_ptr<ByteEventTracker> self,
                                         uint64_t bytesWritten) {
  bool advanceEOM = false;

  while (!byteEvents_.empty() &&
         (byteEvents_.front().byteOffset_ <= bytesWritten)) {
    ByteEvent& event = byteEvents_.front();
    int64_t latency;
    auto txn = event.getTransaction();

    switch (event.eventType_) {
    case ByteEvent::FIRST_HEADER_BYTE:
      txn->onEgressHeaderFirstByte();
      break;
    case ByteEvent::FIRST_BYTE:
      txn->onEgressBodyFirstByte();
      break;
    case ByteEvent::LAST_BYTE:
      txn->onEgressBodyLastByte();
      if (callback_) {
        callback_->onLastByteEvent(txn, event.byteOffset_, event.eomTracked_);
      }
      advanceEOM = true;
      break;
    case ByteEvent::TRACKED_BYTE:
      txn->onEgressTrackedByte();
      break;
    case ByteEvent::PING_REPLY_SENT:
      latency = event.getLatency();
      if (callback_) {
        callback_->onPingReplyLatency(latency);
      }
      break;
    }

    VLOG(5) << " removing ByteEvent " << event;
    // explicitly remove from the list, in case delete event triggers a
    // callback that would absorb this ByteEventTracker.
    event.listHook.unlink();
    delete &event;
  }

  if (advanceEOM) {
    eomEventProcessed();
  }
  return self.use_count() == 1;
}

size_t ByteEventTracker::drainByteEvents() {
  size_t numEvents = 0;
  // everything is dead from here on, let's just drop all extra refs to txns
  while (!byteEvents_.empty()) {
    delete &byteEvents_.front();
    ++numEvents;
  }
  return numEvents;
}

void ByteEventTracker::addLastByteEvent(
    HTTPTransaction* txn,
    uint64_t byteNo) noexcept {
  VLOG(5) << " adding last byte event for " << byteNo;
  TransactionByteEvent* event = new TransactionByteEvent(
      byteNo, ByteEvent::LAST_BYTE, txn);
  byteEvents_.push_back(*event);
}

void ByteEventTracker::addTrackedByteEvent(
    HTTPTransaction* txn,
    uint64_t byteNo) noexcept {
  VLOG(5) << " adding tracked byte event for " << byteNo;
  TransactionByteEvent* event = new TransactionByteEvent(
      byteNo, ByteEvent::TRACKED_BYTE, txn);
  byteEvents_.push_back(*event);
}

void ByteEventTracker::addPingByteEvent(size_t pingSize,
                                        TimePoint timestamp,
                                        uint64_t bytesScheduled) {
  // register a byte event on ping reply sent, and adjust the byteOffset_
  // for others by one ping size
  uint64_t offset = bytesScheduled + pingSize;
  auto i = byteEvents_.rbegin();
  for (; i != byteEvents_.rend(); ++i) {
    if (i->byteOffset_ > bytesScheduled) {
      VLOG(5) << "pushing back ByteEvent from " << *i << " to "
              << ByteEvent(i->byteOffset_ + pingSize, i->eventType_);
      i->byteOffset_ += pingSize;
    } else {
      break; // the rest of the events are already scheduled
    }
  }

  ByteEvent* be = new PingByteEvent(offset, timestamp);
  if (i == byteEvents_.rend()) {
    byteEvents_.push_front(*be);
  } else if (i == byteEvents_.rbegin()) {
    byteEvents_.push_back(*be);
  } else {
    --i;
    CHECK_GT(i->byteOffset_, bytesScheduled);
    byteEvents_.insert(i.base(), *be);
  }
}

void ByteEventTracker::addFirstBodyByteEvent(uint64_t offset,
                                             HTTPTransaction* txn) {
  byteEvents_.push_back(
      *new TransactionByteEvent(
          offset, ByteEvent::FIRST_BYTE,
          txn));
}

void ByteEventTracker::addFirstHeaderByteEvent(uint64_t offset,
                                               HTTPTransaction* txn) {
  // onWriteSuccess() is called after the entire header has been written.
  // It does not catch partial write case.
  byteEvents_.push_back(
      *new TransactionByteEvent(offset,
                                ByteEvent::FIRST_HEADER_BYTE,
                                txn));
}

} // proxygen
