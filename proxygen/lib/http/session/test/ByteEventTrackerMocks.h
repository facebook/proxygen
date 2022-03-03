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

class MockByteEventTrackerCallback
    : public proxygen::ByteEventTracker::Callback {
 public:
#if defined(MOCK_METHOD)
  MOCK_METHOD((void), onPingReplyLatency, (int64_t), (noexcept));
  MOCK_METHOD((void),
              onTxnByteEventWrittenToBuf,
              (const proxygen::ByteEvent&),
              (noexcept));
  MOCK_METHOD((void), onDeleteTxnByteEvent, (), (noexcept));
#else
  GMOCK_METHOD1_(, noexcept, , onPingReplyLatency, void(int64_t));
  GMOCK_METHOD1_(,
                 noexcept,
                 ,
                 onTxnByteEventWrittenToBuf,
                 void(const proxygen::ByteEvent&));
  GMOCK_METHOD0_(, noexcept, , onDeleteTxnByteEvent, void());
#endif
};
