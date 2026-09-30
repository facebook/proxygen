/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/http/coro/util/H3CoroWtSession.h"

namespace proxygen::coro::detail {

H3CoroWtSession::H3CoroWtSession(
    folly::EventBase* evb,
    std::shared_ptr<quic::QuicSocket> quicSocket,
    std::unique_ptr<proxygen::WebTransportHandler> wtHandler,
    proxygen::detail::WtStreamManager::WtConfig wtConfig,
    uint64_t connectStreamId)
    : evb_{evb},
      h3Wt_{std::move(quicSocket),
            std::move(wtHandler),
            wtConfig,
            connectStreamId,
            connectCb_} {
}

H3CoroWtSession::~H3CoroWtSession() noexcept = default;

} // namespace proxygen::coro::detail
