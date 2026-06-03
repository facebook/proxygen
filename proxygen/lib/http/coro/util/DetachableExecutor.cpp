/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/http/coro/util/DetachableExecutor.h"

#include <proxygen/lib/utils/LogShim.h>

namespace proxygen::coro::detail {

void DetachableExecutor::add(folly::Func fn) {
  PRX_VLOG(8) << __func__ << "; pEvb_=" << pEvb_;
  // must be "attached" and running in evb thread
  PRX_CHECK(pEvb_ && pEvb_->isInEventBaseThread());
  auto loopCallback = std::make_unique<LoopCallback>(std::move(fn));
  fnList_.push_back(*loopCallback);
  pEvb_->runInLoop(loopCallback.release()); // deleted after invocation
}

void DetachableExecutor::detachEvb() {
  PRX_VLOG(8) << __func__ << "; pEvb_=" << pEvb_;
  PRX_CHECK(pEvb_->isInEventBaseThread());
  PRX_CHECK_EQ(state_, State::Detachable);
  pEvb_ = nullptr;
  for (auto& loopCallback : fnList_) {
    loopCallback.cancelLoopCallback();
  }
}

void DetachableExecutor::attachEvb(folly::EventBase* evb) {
  PRX_VLOG(8) << __func__ << "; evb=" << evb;
  PRX_CHECK(evb->isInEventBaseThread());
  pEvb_ = evb;
  for (auto& loopCallback : fnList_) {
    pEvb_->runInLoop(&loopCallback);
  }
}

} // namespace proxygen::coro::detail
