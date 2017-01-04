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

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <proxygen/lib/http/HTTPConnector.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/URL.h>

namespace CurlService {

class CurlClient : public proxygen::HTTPConnector::Callback,
                   public proxygen::HTTPTransactionHandler {

 public:
  CurlClient(folly::EventBase* evb, proxygen::HTTPMethod httpMethod,
             const proxygen::URL& url, const proxygen::HTTPHeaders& headers,
             const std::string& inputFilename, bool h2c = false);
  ~CurlClient() override;

  // initial SSL related structures
  void initializeSsl(const std::string& certPath,
                     const std::string& nextProtos);
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

  const std::string& getServerName() const;

  void setFlowControlSettings(int32_t recvWindow);

  const proxygen::HTTPMessage* getResponse() const {
    return response_.get();
  }

  void setLogging(bool enabled) {
    loggingEnabled_ = enabled;
  }

protected:
  proxygen::HTTPTransaction* txn_{nullptr};
  folly::EventBase* evb_{nullptr};
  proxygen::HTTPMethod httpMethod_;
  proxygen::URL url_;
  proxygen::HTTPMessage request_;
  const std::string inputFilename_;
  folly::SSLContextPtr sslContext_;
  int32_t recvWindow_;
  bool loggingEnabled_{true};
  bool h2c_{false};

  std::unique_ptr<proxygen::HTTPMessage> response_;
};

} // CurlService namespace
