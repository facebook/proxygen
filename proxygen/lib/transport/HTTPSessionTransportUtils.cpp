/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/transport/HTTPSessionTransportUtils.h>

using folly::AsyncSocket;

namespace proxygen {

AsyncSocket* getSocketFromTransport(folly::AsyncTransportWrapper* transport) {
  auto current = transport;
  while (current) {
    auto sock = dynamic_cast<AsyncSocket*>(current);
    if (sock) {
      return sock;
    }
    current = current->getWrappedTransport();
  }
  return nullptr;
}

}
