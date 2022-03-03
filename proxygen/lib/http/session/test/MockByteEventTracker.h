/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/session/ByteEventTracker.h>

namespace proxygen {

class MockByteEventTracker : public ByteEventTracker {
 public:
  explicit MockByteEventTracker(Callback* callback)
      : ByteEventTracker(callback) {
  }

  MOCK_METHOD4(addPingByteEvent,
               void(size_t, TimePoint, uint64_t, ByteEvent::Callback));
  MOCK_METHOD3(addFirstBodyByteEvent,
               void(uint64_t, HTTPTransaction*, ByteEvent::Callback));
  MOCK_METHOD3(addFirstHeaderByteEvent,
               void(uint64_t, HTTPTransaction*, ByteEvent::Callback));
  MOCK_METHOD0(drainByteEvents, size_t());
  MOCK_METHOD2(processByteEvents,
               bool(std::shared_ptr<ByteEventTracker>, uint64_t));
  MOCK_METHOD4(preSend, uint64_t(bool*, bool*, bool*, uint64_t));

#if defined(MOCK_METHOD)
  MOCK_METHOD((void),
              addTrackedByteEvent,
              (HTTPTransaction*, uint64_t, ByteEvent::Callback),
              (noexcept));
  MOCK_METHOD((void),
              addLastByteEvent,
              (HTTPTransaction*, uint64_t, ByteEvent::Callback),
              (noexcept));
  MOCK_METHOD(
      (void),
      addTxByteEvent,
      (uint64_t, ByteEvent::EventType, HTTPTransaction*, ByteEvent::Callback),
      (noexcept));
  MOCK_METHOD(
      (void),
      addAckByteEvent,
      (uint64_t, ByteEvent::EventType, HTTPTransaction*, ByteEvent::Callback),
      (noexcept));
#else
  GMOCK_METHOD3_(,
                 noexcept,
                 ,
                 addTrackedByteEvent,
                 void(HTTPTransaction*, uint64_t, ByteEvent::Callback));
  GMOCK_METHOD3_(,
                 noexcept,
                 ,
                 addLastByteEvent,
                 void(HTTPTransaction*, uint64_t, ByteEvent::Callback));
  GMOCK_METHOD4_(,
                 noexcept,
                 ,
                 addTxByteEvent,
                 void(uint64_t,
                      ByteEvent::EventType,
                      HTTPTransaction*,
                      ByteEvent::Callback));
  GMOCK_METHOD4_(,
                 noexcept,
                 ,
                 addAckByteEvent,
                 void(uint64_t,
                      ByteEvent::EventType,
                      HTTPTransaction*,
                      ByteEvent::Callback));
#endif

  // passthru to callback implementation functions
  void onTxnByteEventWrittenToBuf(const ByteEvent& event) {
    callback_->onTxnByteEventWrittenToBuf(event);
  }
};

} // namespace proxygen
