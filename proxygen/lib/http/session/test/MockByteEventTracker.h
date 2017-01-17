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

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/session/ByteEventTracker.h>

namespace proxygen {

class MockByteEventTracker : public ByteEventTracker {
 public:
  explicit MockByteEventTracker(Callback* callback)
      : ByteEventTracker(callback) {}

  MOCK_METHOD3(addPingByteEvent, void(size_t, TimePoint, uint64_t));
  MOCK_METHOD2(addFirstBodyByteEvent, void(uint64_t, HTTPTransaction*));
  MOCK_METHOD2(addFirstHeaderByteEvent, void(uint64_t, HTTPTransaction*));
  MOCK_METHOD0(drainByteEvents, size_t());
  MOCK_METHOD3(processByteEvents, bool(std::shared_ptr<ByteEventTracker>,
                                       uint64_t, bool));
  GMOCK_METHOD3_(, noexcept,, addLastByteEvent,
      void(HTTPTransaction*, uint64_t, bool));
  MOCK_METHOD3(preSend, uint64_t(bool*, bool*, uint64_t));
};

}
