/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/httpserver/Filters.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/lib/utils/StreamDecompressor.h>

#include <cstdint>
#include <optional>

namespace proxygen {

/**
 * A Server filter to perform decompression of incoming request bodies.
 * Supports zstd compression format.
 * If there are any errors it will abort the request. With `maxIngressBytes`
 * set, the handler receives the first `maxIngressBytes` decompressed bytes and
 * then the request is aborted.
 */
class DecompressionFilter : public Filter {
 public:
  explicit DecompressionFilter(
      RequestHandler* downstream,
      std::optional<uint64_t> maxIngressBytes = std::nullopt)
      : Filter(downstream), maxIngressBytes_(maxIngressBytes) {
  }

  ~DecompressionFilter() override = default;

  DecompressionFilter(const DecompressionFilter&) = delete;
  DecompressionFilter& operator=(const DecompressionFilter&) = delete;
  DecompressionFilter(DecompressionFilter&&) = delete;
  DecompressionFilter& operator=(DecompressionFilter&&) = delete;

  void onRequest(std::unique_ptr<HTTPMessage> msg) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

 private:
  // Returns false if decompression failed and the request was aborted.
  bool forwardDecompressed(const folly::IOBuf* in) noexcept;

  std::optional<uint64_t> maxIngressBytes_;
  std::unique_ptr<proxygen::decompression::StreamDecompressor> decompressor_{
      nullptr};
};

/**
 * Factory for creating DecompressionFilter instances.
 */
class DecompressionFilterFactory : public RequestHandlerFactory {
 public:
  explicit DecompressionFilterFactory(
      std::optional<uint64_t> maxIngressBytes = std::nullopt)
      : maxIngressBytes_(maxIngressBytes) {
  }

  ~DecompressionFilterFactory() override = default;

  DecompressionFilterFactory(const DecompressionFilterFactory&) = delete;
  DecompressionFilterFactory& operator=(const DecompressionFilterFactory&) =
      delete;
  DecompressionFilterFactory(DecompressionFilterFactory&&) = delete;
  DecompressionFilterFactory& operator=(DecompressionFilterFactory&&) = delete;

  void onServerStart(folly::EventBase* /*evb*/) noexcept override {
  }

  void onServerStop() noexcept override {
  }

  RequestHandler* onRequest(RequestHandler* h,
                            HTTPMessage* /*msg*/) noexcept override;

 private:
  std::optional<uint64_t> maxIngressBytes_;
};

} // namespace proxygen
