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

#include <folly/io/async/AsyncSocket.h>

namespace proxygen {

/**
 * In many cases when we need to set socket properties on the
 * underlying transport for the HTTPSession. The common way to
 * do this is a dynamic_cast. However we initialize HTTPSession
 * with various transports that are not AsyncSockets. A dynamic_cast
 * would fail in that case. This helper method allows you to get
 * a socket from the HTTPSession and understands the types of transports
 * we store in HTTPSession.
 */
folly::AsyncSocket* getSocketFromTransport(
    folly::AsyncTransportWrapper* transport);

}
