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

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <proxygen/httpclient/URL.h>
#include <proxygen/lib/http/HTTPConnector.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace CurlService {

class CurlClient : public proxygen::HTTPConnector::Callback,
                   public proxygen::HTTPTransactionHandler {

 public:
  CurlClient(folly::EventBase* evb, proxygen::HTTPMethod httpMethod,
      const proxygen::httpclient::URL& url, const std::string& inputFilename);
  ~CurlClient() override;

  // initial SSL related structures
  void initializeSsl(const std::string& certPath);
  void sslHandshakeFollowup(proxygen::HTTPUpstreamSession* session) noexcept;

  // HTTPConnector methods
  void connectSuccess(proxygen::HTTPUpstreamSession* session) override;
  void connectError(const folly::AsyncSocketException& ex) override;

  // HTTPTransactionHandler methods
  void setTransaction(proxygen::HTTPTransaction* txn) noexcept override;
  void detachTransaction() noexcept override;
  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override;
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override;
  void onTrailers(
      std::unique_ptr<proxygen::HTTPHeaders> trailers) noexcept override;
  void onEOM() noexcept override;
  void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override;
  void onError(const proxygen::HTTPException& error) noexcept override;
  void onEgressPaused() noexcept override;
  void onEgressResumed() noexcept override;

  // Getters
  folly::SSLContextPtr getSSLContext() { return sslContext_; }

protected:
  proxygen::HTTPTransaction* txn_{nullptr};
  folly::EventBase* evb_{nullptr};
  proxygen::HTTPMethod httpMethod_;
  proxygen::httpclient::URL url_;
  const std::string inputFilename_;
  folly::SSLContextPtr sslContext_;
};

} // CurlService namespace
