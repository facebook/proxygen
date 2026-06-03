/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/coro/client/CertReloadSessionPool.h>

#include <proxygen/lib/utils/LogShim.h>

namespace proxygen::coro {

CertReloadSessionPool::~CertReloadSessionPool() {
  PRX_CHECK(getEventBase()->isInEventBaseThread());
}

void CertReloadSessionPool::setTimerCallback(
    std::function<void(HTTPCoroSessionPool&)> cb,
    std::chrono::milliseconds interval) {
  PRX_CHECK(getEventBase()->isInEventBaseThread());
  if (interval.count() > 0) {
    reloadTimer_.emplace(*this, std::move(cb), interval);
  }
}

} // namespace proxygen::coro
