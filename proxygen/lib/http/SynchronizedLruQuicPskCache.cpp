/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/SynchronizedLruQuicPskCache.h>

namespace proxygen {

SynchronizedLruQuicPskCache::SynchronizedLruQuicPskCache(uint64_t mapMax)
    : cache_(EvictingPskMap(mapMax)) {
}

folly::Optional<quic::QuicCachedPsk> SynchronizedLruQuicPskCache::getPsk(
    const std::string& identity) {
  auto cacheMap = cache_.wlock();
  auto result = cacheMap->find(identity);
  if (result != cacheMap->end()) {
    return result->second;
  } else {
    return folly::none;
  }
}

void SynchronizedLruQuicPskCache::putPsk(const std::string& identity,
                                         quic::QuicCachedPsk psk) {
  auto cacheMap = cache_.wlock();
  cacheMap->set(identity, std::move(psk));
}

void SynchronizedLruQuicPskCache::removePsk(const std::string& identity) {
  auto cacheMap = cache_.wlock();
  cacheMap->erase(identity);
}

} // namespace proxygen
