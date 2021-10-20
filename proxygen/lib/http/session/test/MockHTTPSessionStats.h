/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>

namespace proxygen {

#define GMOCK_NOEXCEPT_METHOD0(m, F) GMOCK_METHOD0_(, noexcept, , m, F)
#define GMOCK_NOEXCEPT_METHOD1(m, F) GMOCK_METHOD1_(, noexcept, , m, F)
#define GMOCK_NOEXCEPT_METHOD2(m, F) GMOCK_METHOD2_(, noexcept, , m, F)

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
  GMOCK_NOEXCEPT_METHOD0(recordTransactionOpened, void());
  GMOCK_NOEXCEPT_METHOD0(recordTransactionClosed, void());
  GMOCK_NOEXCEPT_METHOD1(recordTransactionsServed, void(uint64_t));
  GMOCK_NOEXCEPT_METHOD0(recordSessionReused, void());
  GMOCK_NOEXCEPT_METHOD1(recordSessionIdleTime, void(std::chrono::seconds));
  GMOCK_NOEXCEPT_METHOD0(recordTransactionStalled, void());
  GMOCK_NOEXCEPT_METHOD0(recordSessionStalled, void());
};

} // namespace proxygen
