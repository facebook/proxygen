/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/logging/xlog.h>
#include <proxygen/lib/http/webtransport/WtEgressContainer.h>

namespace proxygen::coro {

WtBufferedStreamData::FcRes WtBufferedStreamData::enqueue(
    std::unique_ptr<folly::IOBuf> data, bool fin) noexcept {
  XCHECK(!fin_) << "enqueue after fin";
  auto len = data ? data->computeChainDataLength() : 0;
  data_.append(std::move(data)); // ok if nullptr
  fin_ = fin;
  return window_.buffer(len);
}

WtBufferedStreamData::DequeueResult WtBufferedStreamData::dequeue(
    uint64_t atMost) noexcept {
  // min of maxBytes and how many bytes remaining in egress window
  atMost = std::min(atMost,
                    std::min(uint64_t(window_.getAvailable()),
                             uint64_t(data_.chainLength())));
  DequeueResult res;
  res.data = data_.splitAtMost(atMost);
  // Send FIN only if data is empty, fin is pending, and we haven't sent it yet
  res.fin = data_.empty() && fin_ && !finSent_;
  if (res.fin) {
    // IMPORTANT: To maintain stable comparison key for
    // WtStreamManager::Compare, we need onlyFinPending() = data_.empty() &&
    // (fin_ || finSent_) to remain constant during this dequeue call. When we
    // send last chunk + fin together:
    // - Before: data_.empty()=false -> key=false
    // - After: data_.empty()=true -> would become key=true
    // To keep key=false, clear both fin_ and finSent_ after sending last chunk
    // + fin. For fin-only streams, keep fin_=true, finSent_=true -> key remains
    // true.
    if (res.data && res.data->computeChainDataLength() > 0) {
      // Last chunk + fin case: preserve key=false (finSent_ already false)
      fin_ = false;
    } else {
      // Fin only: mark as sent to preserve key=true
      finSent_ = true;
    }
  }
  window_.commit(atMost);
  return res;
}

}; // namespace proxygen::coro
