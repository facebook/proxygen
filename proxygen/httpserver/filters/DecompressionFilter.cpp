/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/httpserver/filters/DecompressionFilter.h>

#include <proxygen/lib/utils/ZstdStreamDecompressor.h>

namespace proxygen {

void DecompressionFilter::onRequest(std::unique_ptr<HTTPMessage> msg) noexcept {
  auto& headers = msg->getHeaders();
  auto contentEncoding =
      headers.getSingleOrNullptr(HTTP_HEADER_CONTENT_ENCODING);
  if (contentEncoding && *contentEncoding == "zstd") {
    decompressor_ =
        std::make_unique<proxygen::decompression::ZstdStreamDecompressor>(
            /*reuseOutBuf=*/false,
            proxygen::decompression::kDefaultMaxZstdDecompressionRatio,
            maxIngressBytes_);
    headers.remove(HTTP_HEADER_CONTENT_ENCODING);
    headers.remove(HTTP_HEADER_CONTENT_LENGTH);
  }
  return Filter::onRequest(std::move(msg));
}

void DecompressionFilter::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (!decompressor_) {
    return Filter::onBody(std::move(body));
  }

  forwardDecompressed(body.get());
}

void DecompressionFilter::onEOM() noexcept {
  if (!decompressor_) {
    return Filter::onEOM();
  }

  folly::IOBuf emptyBuf;
  if (forwardDecompressed(&emptyBuf)) {
    Filter::onEOM();
  }
}

bool DecompressionFilter::forwardDecompressed(const folly::IOBuf* in) noexcept {
  // Past maxIngressBytes the decompressor fails but still returns the bytes up
  // to the cap, so the handler gets them before the abort.
  auto decompressed = decompressor_->decompress(in);
  if (decompressed && !decompressed->empty()) {
    Filter::onBody(std::move(decompressed));
  }
  if (decompressor_->hasError()) {
    Filter::sendAbort(folly::none);
    return false;
  }
  return true;
}

RequestHandler* DecompressionFilterFactory::onRequest(
    RequestHandler* h, HTTPMessage* /*msg*/) noexcept {
  return new DecompressionFilter(h, maxIngressBytes_);
}

} // namespace proxygen
