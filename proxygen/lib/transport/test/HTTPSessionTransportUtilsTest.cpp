/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <folly/io/async/AsyncSocket.h>
#include <proxygen/lib/transport/HTTPSessionTransportUtils.h>
#include <proxygen/lib/transport/DecoratedAsyncTransportWrapper.h>

using namespace proxygen;
using namespace folly;

TEST(TransportUtilsTest, GetSocketFromSocket) {
  AsyncSocket::UniquePtr transport(new AsyncSocket());
  auto sock = getSocketFromTransport(transport.get());
  ASSERT_EQ(transport.get(), sock);
}

TEST(TransportUtilsTest, GetSocketFromDecoratedTransport) {
  AsyncSocket::UniquePtr transport(new AsyncSocket());
  auto transportAddr = transport.get();

  AsyncTransportWrapper::UniquePtr wrapped1(
      new DecoratedAsyncTransportWrapper<AsyncSocket>(std::move(transport)));
  AsyncTransportWrapper::UniquePtr wrapped2(
      new DecoratedAsyncTransportWrapper<AsyncTransportWrapper>(
        std::move(wrapped1)));
  auto sock = getSocketFromTransport(wrapped2.get());
  ASSERT_EQ(transportAddr, sock);
}
