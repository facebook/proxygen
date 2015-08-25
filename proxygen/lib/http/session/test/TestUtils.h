/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/async/EventBase.h>
#include <folly/io/async/TimeoutManager.h>
#include <gtest/gtest.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <folly/io/async/test/MockAsyncTransport.h>

namespace proxygen {

extern const wangle::TransportInfo mockTransportInfo;
extern const folly::SocketAddress localAddr;
extern const folly::SocketAddress peerAddr;

folly::HHWheelTimer::UniquePtr
makeInternalTimeoutSet(folly::EventBase* evb);

folly::HHWheelTimer::UniquePtr
makeTimeoutSet(folly::EventBase* evb);

testing::NiceMock<folly::test::MockAsyncTransport>*
newMockTransport(folly::EventBase* evb);

}
