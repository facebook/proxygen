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
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/DestructorCheck.h>

namespace proxygen {

static const std::string kMessageFilterDefaultName_ = "Unknown";

class HTTPMessageFilter: public HTTPTransaction::Handler,
                         public DestructorCheck {
 public:
  void setNextTransactionHandler(HTTPTransaction::Handler* next) {
    nextTransactionHandler_ = CHECK_NOTNULL(next);
  }
  HTTPTransaction::Handler* getNextTransactionHandler() {
    return nextTransactionHandler_;
  }

  virtual std::unique_ptr<HTTPMessageFilter> clone () noexcept = 0;

  // These HTTPTransaction::Handler callbacks may be overwritten
  // The default behavior is to pass the call through.
  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    nextTransactionHandler_->onHeadersComplete(std::move(msg));
  }
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    nextTransactionHandler_->onBody(std::move(chain));
  }
  void onChunkHeader(size_t length) noexcept override {
    nextTransactionHandler_->onChunkHeader(length);
  }
  void onChunkComplete() noexcept override {
    nextTransactionHandler_->onChunkComplete();
  }
  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    nextTransactionHandler_->onTrailers(std::move(trailers));
  }
  void onEOM() noexcept override {
    nextTransactionHandler_->onEOM();
  }
  void onUpgrade(UpgradeProtocol protocol) noexcept override {
    nextTransactionHandler_->onUpgrade(protocol);
  }
  void onError(const HTTPException& error) noexcept override {
    nextTransactionHandler_->onError(error);
  }

  // These HTTPTransaction::Handler callbacks cannot be overrwritten
  void setTransaction(HTTPTransaction* txn) noexcept final {
    nextTransactionHandler_->setTransaction(txn);
  }
  void detachTransaction() noexcept final {
    nextTransactionHandler_->detachTransaction();
  }
  void onEgressPaused() noexcept final {
    nextTransactionHandler_->onEgressPaused();
  }
  void onEgressResumed() noexcept final {
    nextTransactionHandler_->onEgressResumed();
  }
  void onPushedTransaction(HTTPTransaction* txn) noexcept final {
    nextTransactionHandler_->onPushedTransaction(txn);
  }
  virtual const std::string& getFilterName() noexcept {
    return kMessageFilterDefaultName_;
  }
 protected:
  void nextOnHeadersComplete(std::unique_ptr<HTTPMessage> msg) {
    nextTransactionHandler_->onHeadersComplete(std::move(msg));
  }
  void nextOnBody(std::unique_ptr<folly::IOBuf> chain) {
    nextTransactionHandler_->onBody(std::move(chain));
  }
  void nextOnChunkHeader(size_t length) {
    nextTransactionHandler_->onChunkHeader(length);
  }
  void nextOnChunkComplete() {
    nextTransactionHandler_->onChunkComplete();
  }
  void nextOnTrailers(std::unique_ptr<HTTPHeaders> trailers) {
    nextTransactionHandler_->onTrailers(std::move(trailers));
  }
  void nextOnEOM() {
    nextTransactionHandler_->onEOM();
  }
  void nextOnError(const HTTPException& ex) {
    nextTransactionHandler_->onError(ex);
  }
  HTTPTransaction::Handler* nextTransactionHandler_{nullptr};
};

} // proxygen
