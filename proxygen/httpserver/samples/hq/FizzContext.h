/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fizz/client/FizzClientContext.h>
#include <fizz/server/FizzServerContext.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <wangle/ssl/SSLContextConfig.h>

namespace quic { namespace samples {

using FizzServerContextPtr =
    std::shared_ptr<const fizz::server::FizzServerContext>;

using FizzClientContextPtr = std::shared_ptr<fizz::client::FizzClientContext>;

FizzServerContextPtr createFizzServerContext(const HQParams& params);

FizzClientContextPtr createFizzClientContext(const HQParams& params);

wangle::SSLContextConfig createSSLContext(const HQParams& params);
}} // namespace quic::samples
