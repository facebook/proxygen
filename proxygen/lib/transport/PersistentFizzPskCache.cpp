/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/transport/PersistentFizzPskCache.h>

#include <fizz/record/Types.h>

using namespace folly;

namespace folly {

template <>
dynamic toDynamic(const proxygen::PersistentCachedPsk& cached) {
  dynamic d = dynamic::object;
  d["psk"] = cached.serialized;
  d["uses"] = cached.uses;
  return d;
}

template <>
proxygen::PersistentCachedPsk convertTo(const dynamic& d) {
  proxygen::PersistentCachedPsk psk;
  psk.serialized = d["psk"].asString();
  psk.uses = folly::to<size_t>(d["uses"].asInt());
  return psk;
}
} // namespace folly
