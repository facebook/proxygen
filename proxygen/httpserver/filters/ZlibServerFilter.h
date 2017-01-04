/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Memory.h>

#include <proxygen/httpserver/Filters.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/lib/utils/ZlibStreamCompressor.h>
#include <proxygen/lib/http/RFC2616.h>

namespace proxygen {

/**
 * A Server filter to perform GZip compression. If there are any errors it will
 * fall back to sending uncompressed responses.
 */
class ZlibServerFilter : public Filter {
 public:
  explicit ZlibServerFilter(
      RequestHandler* downstream,
      int32_t compressionLevel,
      uint32_t minimumCompressionSize,
      const std::shared_ptr<std::set<std::string>> compressibleContentTypes)
      : Filter(downstream),
        compressionLevel_(compressionLevel),
        minimumCompressionSize_(minimumCompressionSize),
        compressibleContentTypes_(compressibleContentTypes) {}

  void sendHeaders(HTTPMessage& msg) noexcept override {
    DCHECK(compressor_ == nullptr);
    DCHECK(header_ == false);

    chunked_ = msg.getIsChunked();

    // Make final determination of whether to compress
    compress_ = isCompressibleContentType(msg) &&
      (chunked_ || isMinimumCompressibleSize(msg));

    // Add the gzip header
    if (compress_) {
      auto& headers = msg.getHeaders();
      headers.set(HTTP_HEADER_CONTENT_ENCODING, "gzip");
    }

    // If it's chunked or not being compressed then the headers can be sent
    // if it's compressed and one body, then need to calculate content length.
    if (chunked_ || !compress_) {
      Filter::sendHeaders(msg);
      header_ = true;
    } else {
      responseMessage_ = folly::make_unique<HTTPMessage>(msg);
    }
  }

  void sendChunkHeader(size_t len) noexcept override {
    // The headers should have always been sent since the message is chunked
    DCHECK_EQ(header_, true) << "Headers should have already been sent.";

    // If not compressing, pass downstream, otherwise "swallow" it
    // to send after compressing the body.
    if (!compress_) {
      Filter::sendChunkHeader(len);
    }

    // Return without sending the chunk header.
    return;
  }

  // Compress the body, if chunked may be called multiple times
  void sendBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    // If not compressing, pass the body through
    if (!compress_) {
      DCHECK(header_ == true);
      Filter::sendBody(std::move(body));
      return;
    }

    //First time through the compressor
    if (compressor_ == nullptr) {
      compressor_ = folly::make_unique<ZlibStreamCompressor>(
          proxygen::ZlibCompressionType::GZIP, compressionLevel_);

      if (!compressor_ || compressor_->hasError()) {
        return fail();
      }
    }
    DCHECK(compressor_ != nullptr);

    // If it's chunked, never write the trailer, it will be written on EOM
    auto compressed = compressor_->compress(body.get(), !chunked_);
    if (compressor_->hasError()) {
      return fail();
    }

    auto compressedBodyLength = compressed->computeChainDataLength();

    if (chunked_) {
        // Send on the swallowed chunk header.
        Filter::sendChunkHeader(compressedBodyLength);
    } else {
      //Send the content length on compressed, non-chunked messages
      DCHECK(header_ == false);
      DCHECK(compress_ == true);
      auto& headers = responseMessage_->getHeaders();
      headers.set(HTTP_HEADER_CONTENT_LENGTH,
          folly::to<std::string>(compressedBodyLength));

      Filter::sendHeaders(*responseMessage_);
      header_  = true;
    }

    Filter::sendBody(std::move(compressed));
  }

  void sendEOM() noexcept override {

    // Need to send the gzip trailer for compressed chunked messages
    if (compress_ && chunked_) {

      auto emptyBuffer = folly::IOBuf::copyBuffer("");
      auto compressed = compressor_->compress(emptyBuffer.get(), true);

      if (compressor_->hasError()) {
        return fail();
      }

      // "Inject" a chunk with the gzip trailer.
      Filter::sendChunkHeader(compressed->computeChainDataLength());
      Filter::sendBody(std::move(compressed));
      Filter::sendChunkTerminator();
    }

    Filter::sendEOM();
  }

 protected:

  void fail() {
    Filter::sendAbort();
  }

  //Verify the response is large enough to compress
  bool isMinimumCompressibleSize(const HTTPMessage& msg) const noexcept {
    auto contentLengthHeader =
        msg.getHeaders().getSingleOrEmpty(HTTP_HEADER_CONTENT_LENGTH);

    uint32_t contentLength = 0;
    if (!contentLengthHeader.empty()) {
      contentLength = folly::to<uint32_t>(contentLengthHeader);
    }

    return contentLength > minimumCompressionSize_;
  }

  // Check the response's content type against a list of compressible types
  bool isCompressibleContentType(const HTTPMessage& msg) const noexcept {

    auto responseContentType =
        msg.getHeaders().getSingleOrEmpty(HTTP_HEADER_CONTENT_TYPE);

    folly::toLowerAscii((char *)responseContentType.data(),
        responseContentType.size());

    // Handle  text/html; encoding=utf-8 case
    auto parameter_idx = responseContentType.find(';');
    if (parameter_idx != std::string::npos) {
     responseContentType = responseContentType.substr(0, parameter_idx);
    }

    auto idx = compressibleContentTypes_->find(responseContentType);

    if (idx != compressibleContentTypes_->end()) {
      return true;
    }

    return false;
  }

  std::unique_ptr<HTTPMessage> responseMessage_;
  std::unique_ptr<ZlibStreamCompressor> compressor_{nullptr};
  int32_t compressionLevel_{4};
  uint32_t minimumCompressionSize_{1000};
  const std::shared_ptr<std::set<std::string>> compressibleContentTypes_;
  bool header_{false};
  bool chunked_{false};
  bool compress_{false};
};

class ZlibServerFilterFactory : public RequestHandlerFactory {
 public:
  explicit ZlibServerFilterFactory(
      int32_t compressionLevel,
      uint32_t minimumCompressionSize,
      const std::set<std::string> compressibleContentTypes)
      : compressionLevel_(compressionLevel),
        minimumCompressionSize_(minimumCompressionSize),
        compressibleContentTypes_(
            std::make_shared<std::set<std::string>>(compressibleContentTypes)) {
  }

  void onServerStart(folly::EventBase* evb) noexcept override {}

  void onServerStop() noexcept override {}

  RequestHandler* onRequest(RequestHandler* h,
                            HTTPMessage* msg) noexcept override {

    if (acceptsSupportedCompressionType(msg)) {
      auto zlibServerFilter =
          new ZlibServerFilter(h,
              compressionLevel_,
              minimumCompressionSize_,
              compressibleContentTypes_);
      return zlibServerFilter;
    }

    // No compression
    return h;
  }

 protected:

  // Check whether the client supports a compression type we support
  bool acceptsSupportedCompressionType(HTTPMessage* msg) noexcept {

    std::vector<RFC2616::TokenQPair> output;

    //Accept encoding header could have qvalues (gzip; q=5.0)
    auto acceptEncodingHeader =
        msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_ACCEPT_ENCODING);

    if (RFC2616::parseQvalues(acceptEncodingHeader, output)) {
      std::vector<RFC2616::TokenQPair>::iterator it = std::find_if(
          output.begin(), output.end(), [](RFC2616::TokenQPair elem) {
            return elem.first.compare(folly::StringPiece("gzip")) == 0;
          });

      return (it != output.end());
    }

    return false;
  }

  int32_t compressionLevel_;
  uint32_t minimumCompressionSize_;
  const std::shared_ptr<std::set<std::string>> compressibleContentTypes_;
};
}
