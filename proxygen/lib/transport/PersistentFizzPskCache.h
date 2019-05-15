/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <fizz/client/PskCache.h>
#include <fizz/protocol/Factory.h>
#include <wangle/client/persistence/FilePersistentCache.h>

namespace proxygen {

std::string serializePsk(const fizz::client::CachedPsk& psk);
fizz::client::CachedPsk deserializePsk(const std::string& str,
                                       const fizz::Factory& factory);

struct PersistentCachedPsk {
  std::string serialized;
  size_t uses{0};
};

class PersistentFizzPskCache : public fizz::client::PskCache {
 public:
  ~PersistentFizzPskCache() override = default;

  PersistentFizzPskCache(const std::string& filename,
                         wangle::PersistentCacheConfig config,
                         std::unique_ptr<fizz::Factory> factory =
                             std::make_unique<fizz::Factory>())
      : cache_(filename, std::move(config)), factory_(std::move(factory)) {
  }

  void setMaxPskUses(size_t maxUses) {
    maxPskUses_ = maxUses;
  }

  folly::Optional<fizz::client::CachedPsk> getPsk(
      const std::string& identity) override {
    auto serialized = cache_.get(identity);
    if (serialized) {
      try {
        auto deserialized = deserializePsk(serialized->serialized, *factory_);
        serialized->uses++;
        if (maxPskUses_ != 0 && serialized->uses >= maxPskUses_) {
          cache_.remove(identity);
        } else {
          cache_.put(identity, *serialized);
        }
        return std::move(deserialized);
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Error deserializing PSK: " << ex.what();
        cache_.remove(identity);
      }
    }
    return folly::none;
  }

  void putPsk(const std::string& identity,
              fizz::client::CachedPsk psk) override {
    PersistentCachedPsk serialized;
    serialized.serialized = serializePsk(psk);
    serialized.uses = 0;
    cache_.put(identity, std::move(serialized));
  }

  void removePsk(const std::string& identity) override {
    cache_.remove(identity);
  }

 private:
  wangle::FilePersistentCache<std::string, PersistentCachedPsk> cache_;

  size_t maxPskUses_{5};

  std::unique_ptr<fizz::Factory> factory_;
};
}

namespace folly {

template <>
dynamic toDynamic(const proxygen::PersistentCachedPsk& cached);
template <>
proxygen::PersistentCachedPsk convertTo(const dynamic& d);
}
