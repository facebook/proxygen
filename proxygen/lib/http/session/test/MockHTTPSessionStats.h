/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>

namespace proxygen {

class DummyHTTPSessionStats : public HTTPSessionStats {
 public:
  void recordTransactionOpened() noexcept override{};
  void recordTransactionClosed() noexcept override{};
  void recordTransactionsServed(uint64_t) noexcept override{};
  void recordSessionReused() noexcept override{};
  // virtual void recordSessionIdleTime(std::chrono::seconds) noexcept {};
  void recordTransactionStalled() noexcept override{};
  void recordSessionStalled() noexcept override{};

  void recordPresendIOSplit() noexcept override{};
  void recordPresendExceedLimit() noexcept override{};
  void recordTTLBAExceedLimit() noexcept override{};
  void recordTTLBANotFound() noexcept override{};
  void recordTTLBAReceived() noexcept override{};
  void recordTTLBATimeout() noexcept override{};
  void recordTTLBATracked() noexcept override{};
  void recordTTBTXExceedLimit() noexcept override{};
  void recordTTBTXReceived() noexcept override{};
  void recordTTBTXTimeout() noexcept override{};
  void recordTTBTXNotFound() noexcept override{};
  void recordTTBTXTracked() noexcept override{};
};

class MockHTTPSessionStats : public DummyHTTPSessionStats {
 public:
  MockHTTPSessionStats() {
  }
  void recordTransactionOpened() noexcept override {
    _recordTransactionOpened();
  }
  MOCK_METHOD0(_recordTransactionOpened, void());
  void recordTransactionClosed() noexcept override {
    _recordTransactionClosed();
  }
  MOCK_METHOD0(_recordTransactionClosed, void());
  void recordTransactionsServed(uint64_t num) noexcept override {
    _recordTransactionsServed(num);
  }
  MOCK_METHOD1(_recordTransactionsServed, void(uint64_t));
  void recordSessionReused() noexcept override {
    _recordSessionReused();
  }
  MOCK_METHOD0(_recordSessionReused, void());
  void recordSessionIdleTime(std::chrono::seconds param) noexcept override {
    _recordSessionIdleTime(param);
  }
  MOCK_METHOD1(_recordSessionIdleTime, void(std::chrono::seconds));
  void recordTransactionStalled() noexcept override {
    _recordTransactionStalled();
  }
  MOCK_METHOD0(_recordTransactionStalled, void());
  void recordSessionStalled() noexcept override {
    _recordSessionStalled();
  }
  MOCK_METHOD0(_recordSessionStalled, void());
  void recordPendingBufferedReadBytes(int64_t num) noexcept override {
    _recordPendingBufferedReadBytes(num);
  }
  MOCK_METHOD1(_recordPendingBufferedReadBytes, void(int64_t));
};

} // namespace proxygen
