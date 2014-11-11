/*
 *  Copyright (c) 2014, Facebook, Inc.
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
#include <thrift/lib/cpp/test/MockTAsyncTransport.h>

namespace proxygen {

extern const TransportInfo mockTransportInfo;
extern const folly::SocketAddress localAddr;
extern const folly::SocketAddress peerAddr;

apache::thrift::async::TAsyncTimeoutSet::UniquePtr
makeInternalTimeoutSet(folly::EventBase* evb);

apache::thrift::async::TAsyncTimeoutSet::UniquePtr
makeTimeoutSet(folly::EventBase* evb);

testing::NiceMock<apache::thrift::test::MockTAsyncTransport>*
newMockTransport(folly::EventBase* evb);

}
