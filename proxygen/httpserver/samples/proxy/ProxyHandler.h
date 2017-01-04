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
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/lib/http/HTTPConnector.h>
#include "SessionWrapper.h"

namespace proxygen {
class ResponseHandler;
}

namespace ProxyService {

class ProxyStats;

class ProxyHandler : public proxygen::RequestHandler,
                     private proxygen::HTTPConnector::Callback {
 public:
  ProxyHandler(ProxyStats* stats, folly::HHWheelTimer* timer);

  ~ProxyHandler();

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
      noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override {}

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

  void onEgressPaused() noexcept override;

  void onEgressResumed() noexcept override;

  void detachServerTransaction() noexcept;
  void onServerHeadersComplete(
    std::unique_ptr<proxygen::HTTPMessage> msg) noexcept;
  void onServerBody(std::unique_ptr<folly::IOBuf> chain) noexcept;
  void onServerEOM() noexcept;
  void onServerError(const proxygen::HTTPException& error) noexcept;
  void onServerEgressPaused() noexcept;
  void onServerEgressResumed() noexcept;

 private:

  void connectSuccess(proxygen::HTTPUpstreamSession* session) override;
  void connectError(const folly::AsyncSocketException& ex) override;

  class ServerTransactionHandler: public proxygen::HTTPTransactionHandler {
   public:
    explicit ServerTransactionHandler(ProxyHandler& parent)
        : parent_(parent) {
    }
   private:
    ProxyHandler& parent_;

    void setTransaction(proxygen::HTTPTransaction* txn) noexcept override {
      // no op
    }
    void detachTransaction() noexcept override {
      parent_.detachServerTransaction();
    }
    void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
      parent_.onServerHeadersComplete(std::move(msg));
    }

    void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
      parent_.onServerBody(std::move(chain));
    }

    void onTrailers(
      std::unique_ptr<proxygen::HTTPHeaders> trailers) noexcept override {
      // ignore for now
    }
    void onEOM() noexcept override {
      parent_.onServerEOM();
    }
    void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override {
      // ignore for now
    }

    void onError(const proxygen::HTTPException& error) noexcept override {
      parent_.onServerError(error);
    }

    void onEgressPaused() noexcept override {
      parent_.onServerEgressPaused();
    }
    void onEgressResumed() noexcept override {
      parent_.onServerEgressPaused();
    }

  };

  bool checkForShutdown();

  ProxyStats* const stats_{nullptr};
  proxygen::HTTPConnector connector_;
  ServerTransactionHandler serverHandler_;
  std::unique_ptr<SessionWrapper> session_;
  proxygen::HTTPTransaction* txn_{nullptr};
  bool clientTerminated_{false};

  std::unique_ptr<proxygen::HTTPMessage> request_;
  std::unique_ptr<folly::IOBuf> body_;
};

}
