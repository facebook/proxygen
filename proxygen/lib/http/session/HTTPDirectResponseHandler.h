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

#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace proxygen {

class HTTPErrorPage;

class HTTPDirectResponseHandler:
  public HTTPTransaction::Handler {
public:
  HTTPDirectResponseHandler(unsigned statusCode, const std::string& statusMsg,
      const HTTPErrorPage* errorPage = nullptr);

  void forceConnectionClose(bool close) {
    forceConnectionClose_ = close;
  }
  // HTTPTransaction::Handler methods
  void setTransaction(HTTPTransaction* txn) noexcept;
  void detachTransaction() noexcept;
  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept;
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept;
  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept;
  void onEOM() noexcept;
  void onUpgrade(UpgradeProtocol protocol) noexcept override;
  void onError(const HTTPException& error) noexcept;
  // These are no-ops since the direct response is already in memory
  void onEgressPaused() noexcept {};
  void onEgressResumed() noexcept {};

private:
  ~HTTPDirectResponseHandler();

  HTTPTransaction* txn_;
  const HTTPErrorPage* errorPage_;
  std::string statusMessage_;
  unsigned statusCode_;
  bool headersSent_:1;
  bool eomSent_:1;
  bool forceConnectionClose_:1;
};

} // proxygen
