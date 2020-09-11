/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <gmock/gmock.h>

#include <proxygen/lib/http/HTTPMessageFilters.h>

namespace proxygen {

static const std::string kMockFilterName = "MockFilter";

class MockHTTPMessageFilter : public HTTPMessageFilter {
 public:
  MOCK_QUALIFIED_METHOD1(onHeadersComplete,
                         noexcept,
                         void(std::shared_ptr<HTTPMessage>));
  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    onHeadersComplete(std::shared_ptr<HTTPMessage>(msg.release()));
  }

  MOCK_QUALIFIED_METHOD1(onBody, noexcept, void(std::shared_ptr<folly::IOBuf>));
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    if (trackDataPassedThrough_) {
      bodyDataReceived_.append(chain->clone());
    }
    onBody(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  MOCK_QUALIFIED_METHOD0(pause, noexcept, void());
  MOCK_QUALIFIED_METHOD1(onChunkHeader, noexcept, void(size_t));
  MOCK_QUALIFIED_METHOD1(resume, noexcept, void(uint64_t));
  MOCK_QUALIFIED_METHOD0(onChunkComplete, noexcept, void());
  MOCK_QUALIFIED_METHOD1(onTrailers,
                         noexcept,
                         void(std::shared_ptr<HTTPHeaders> trailers));
  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    onTrailers(std::shared_ptr<HTTPHeaders>(trailers.release()));
  }
  MOCK_QUALIFIED_METHOD0(onEOM, noexcept, void());
  MOCK_QUALIFIED_METHOD1(onUpgrade, noexcept, void(UpgradeProtocol));
  MOCK_QUALIFIED_METHOD1(onError, noexcept, void(const HTTPException&));

  void nextOnHeadersCompletePublic(std::shared_ptr<HTTPMessage> msg) {
    std::unique_ptr<HTTPMessage> msgU(new HTTPMessage(*msg));
    nextOnHeadersComplete(std::move(msgU));
  }

  const std::string& getFilterName() noexcept override {
    return kMockFilterName;
  }

  boost::variant<HTTPMessageFilter*, HTTPTransaction*> getPrevElement() {
    return prev_;
  }

  [[noreturn]] std::unique_ptr<HTTPMessageFilter> clone() noexcept override {
    LOG(FATAL) << "clone() not implemented for filter: "
               << this->getFilterName();
  };

  void nextOnEOMPublic() {
    nextOnEOM();
  }

  std::unique_ptr<folly::IOBuf> bodyDataSinceLastCheck() {
    return bodyDataReceived_.move();
  }

  void setTrackDataPassedThrough(bool track) {
    trackDataPassedThrough_ = track;
  }

 private:
  folly::IOBufQueue bodyDataReceived_{folly::IOBufQueue::cacheChainLength()};
  bool trackDataPassedThrough_{false};
};

} // namespace proxygen
