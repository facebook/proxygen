/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/HTTPMessageFilters.h>

namespace proxygen {

class MockHTTPMessageFilter : public HTTPMessageFilter {
 public:
  GMOCK_METHOD1_(, noexcept,, onHeadersComplete,
                 void(std::shared_ptr<HTTPMessage>));
  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    onHeadersComplete(std::shared_ptr<HTTPMessage>(msg.release()));
  }

  GMOCK_METHOD1_(, noexcept,, onBody, void(std::shared_ptr<folly::IOBuf>));
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    onBody(std::shared_ptr<folly::IOBuf>(chain.release()));
  }
  GMOCK_METHOD1_(, noexcept,, onChunkHeader, void(size_t));
  GMOCK_METHOD0_(, noexcept,, onChunkComplete, void());
  GMOCK_METHOD1_(, noexcept,, onTrailers,
                 void(std::shared_ptr<HTTPHeaders> trailers));
  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    onTrailers(std::shared_ptr<HTTPHeaders>(trailers.release()));
  }
  GMOCK_METHOD0_(, noexcept,, onEOM, void());
  GMOCK_METHOD1_(, noexcept,, onUpgrade, void(UpgradeProtocol));
  GMOCK_METHOD1_(, noexcept,, onError, void(const HTTPException&));

  void nextOnHeadersCompletePublic(std::shared_ptr<HTTPMessage> msg) {
    std::unique_ptr<HTTPMessage> msgU(new HTTPMessage(*msg));
    nextOnHeadersComplete(std::move(msgU));
  }

  void nextOnEOMPublic() {
    nextOnEOM();
  }
};

}
