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

#include <folly/SocketAddress.h>

namespace proxygen {

struct AcceptorAddress {
  enum class AcceptorType { TCP, UDP };

  AcceptorAddress() = delete;
  AcceptorAddress(folly::SocketAddress address, AcceptorType protocol)
      : address(address), protocol(protocol) {
  }

  folly::SocketAddress address;
  AcceptorType protocol;
};

inline bool operator<(const AcceptorAddress& lv, const AcceptorAddress& rv) {
  if (lv.address < rv.address) {
    return true;
  }
  if (rv.address < lv.address) {
    return false;
  }
  return lv.protocol < rv.protocol;
}

inline std::ostream& operator<<(std::ostream& os,
                                const AcceptorAddress::AcceptorType& accType) {
  switch (accType) {
    case AcceptorAddress::AcceptorType::TCP:
      os << "TCP";
      break;
    case AcceptorAddress::AcceptorType::UDP:
      os << "UDP";
      break;
    default:
      LOG(FATAL) << "Unknown Acceptor type.";
  }
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const AcceptorAddress& accAddr) {
  os << accAddr.address << "<" << accAddr.protocol << ">";
  return os;
}

using AcceptorType = AcceptorAddress::AcceptorType;

} // namespace proxygen
