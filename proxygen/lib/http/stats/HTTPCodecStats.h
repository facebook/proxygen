/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Optional.h>
#include <proxygen/lib/http/codec/CodecProtocol.h>
#include <proxygen/lib/http/codec/ErrorCode.h>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/stats/StatsWrapper.h>
#include <string>

namespace proxygen {

/**
 * A stats interface for tracking HTTP events, primarily those on parallel
 * protocols.
 */
class HTTPCodecStats {
 public:
  virtual ~HTTPCodecStats() = default;

  virtual void incrementParallelConn(int64_t amount) = 0;

  virtual void recordIngressSynStream() = 0;
  virtual void recordIngressSynReply() = 0;
  virtual void recordIngressPushPromise() = 0;
  virtual void recordIngressData() = 0;
  virtual void recordIngressRst(ErrorCode statusCode) = 0;
  virtual void recordIngressSettings() = 0;
  virtual void recordIngressPingRequest() = 0;
  virtual void recordIngressPingReply() = 0;
  virtual void recordIngressGoaway(ErrorCode statusCode) = 0;
  virtual void recordIngressGoawayDrain() = 0;
  virtual void recordIngressWindowUpdate() = 0;
  virtual void recordIngressPriority() = 0;

  virtual void recordEgressSynStream() = 0;
  virtual void recordEgressSynReply() = 0;
  virtual void recordEgressPushPromise() = 0;
  virtual void recordEgressData() = 0;
  virtual void recordEgressRst(ErrorCode statusCode) = 0;
  virtual void recordEgressSettings() = 0;
  virtual void recordEgressPingRequest() = 0;
  virtual void recordEgressPingReply() = 0;
  virtual void recordEgressGoaway(ErrorCode statusCode) = 0;
  virtual void recordEgressGoawayDrain() = 0;
  virtual void recordEgressWindowUpdate() = 0;
  virtual void recordEgressPriority() = 0;
};

/**
 * A TCSD implementation of HTTPCodecStats
 */
class TLHTTPCodecStats : public HTTPCodecStats {
 public:
  // Given the session's protocol and direction, skips the counters such a
  // session can never record.
  explicit TLHTTPCodecStats(
      const std::string& prefix,
      folly::Optional<CodecProtocol> protocol = folly::none,
      folly::Optional<TransportDirection> direction = folly::none);
  explicit TLHTTPCodecStats(const TLHTTPCodecStats&) = delete;
  TLHTTPCodecStats& operator=(const TLHTTPCodecStats&) = delete;
  ~TLHTTPCodecStats() override = default;

  void incrementParallelConn(int64_t amount) override;

  void recordIngressSynStream() override;
  void recordIngressSynReply() override;
  void recordIngressPushPromise() override;
  void recordIngressData() override;
  void recordIngressRst(ErrorCode statusCode) override;
  void recordIngressSettings() override;
  void recordIngressPingRequest() override;
  void recordIngressPingReply() override;
  void recordIngressGoaway(ErrorCode statusCode) override;
  void recordIngressGoawayDrain() override;
  void recordIngressWindowUpdate() override;
  void recordIngressPriority() override;

  void recordEgressSynStream() override;
  void recordEgressSynReply() override;
  void recordEgressPushPromise() override;
  void recordEgressData() override;
  void recordEgressRst(ErrorCode statusCode) override;
  void recordEgressSettings() override;
  void recordEgressPingRequest() override;
  void recordEgressPingReply() override;
  void recordEgressGoaway(ErrorCode statusCode) override;
  void recordEgressGoawayDrain() override;
  void recordEgressWindowUpdate() override;
  void recordEgressPriority() override;

 private:
  std::string prefix_;
  StatsWrapper::TLCounter openConn_;
  StatsWrapper::TLTimeseries ingressSynStream_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressSynReply_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressPushPromise_;
  StatsWrapper::TLTimeseries ingressData_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressRst_;
  std::vector<folly::Optional<StatsWrapper::TLTimeseries>> ingressRstStatus_;
  StatsWrapper::TLTimeseries ingressSettings_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressPingRequest_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressPingReply_;
  StatsWrapper::TLTimeseries ingressGoaway_;
  StatsWrapper::TLTimeseries ingressGoawayDrain_;
  std::vector<folly::Optional<StatsWrapper::TLTimeseries>> ingressGoawayStatus_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressWindowUpdate_;
  folly::Optional<StatsWrapper::TLTimeseries> ingressPriority_;

  folly::Optional<StatsWrapper::TLTimeseries> egressSynStream_;
  folly::Optional<StatsWrapper::TLTimeseries> egressSynReply_;
  folly::Optional<StatsWrapper::TLTimeseries> egressPushPromise_;
  StatsWrapper::TLTimeseries egressData_;
  folly::Optional<StatsWrapper::TLTimeseries> egressRst_;
  std::vector<folly::Optional<StatsWrapper::TLTimeseries>> egressRstStatus_;
  StatsWrapper::TLTimeseries egressSettings_;
  folly::Optional<StatsWrapper::TLTimeseries> egressPingRequest_;
  folly::Optional<StatsWrapper::TLTimeseries> egressPingReply_;
  StatsWrapper::TLTimeseries egressGoaway_;
  StatsWrapper::TLTimeseries egressGoawayDrain_;
  std::vector<folly::Optional<StatsWrapper::TLTimeseries>> egressGoawayStatus_;
  folly::Optional<StatsWrapper::TLTimeseries> egressWindowUpdate_;
  StatsWrapper::TLTimeseries egressPriority_;
};

} // namespace proxygen
