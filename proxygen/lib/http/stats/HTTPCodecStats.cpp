/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/stats/HTTPCodecStats.h>

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/logging/xlog.h>

using facebook::fb303::RATE;
using facebook::fb303::SUM;

namespace {
static std::array<const char*, 14> kErrorStrings{
    "Ok",
    "Protocol_Error",
    "Internal_Error",
    "Flow_Control_Error",
    "Settings_Timeout",
    "Stream_Closed",
    "Frame_Size_Error",
    "Refused_Stream",
    "Cancel",
    "Compression_Error",
    "Connect_Error",
    "Enhance_Your_Calm",
    "Inadequate_Security",
    "Http_1_1_Required",
};

} // namespace

namespace proxygen {

namespace {

// TLHTTPCodecStats only skips counters that a session of its protocol and
// direction can never record, so recording one anyway means that reasoning no
// longer holds.
void addOrLogSkipped(folly::Optional<StatsWrapper::TLTimeseries>& counter,
                     const std::string& prefix,
                     folly::StringPiece name,
                     folly::StringPiece code = "") {
  if (counter) {
    counter->add(1);
    return;
  }
  XLOG_EVERY_N(ERR, 1000) << "Recorded " << prefix << "_" << name << code
                          << ", which TLHTTPCodecStats skipped as impossible "
                             "for this protocol and direction";
}

} // namespace

// TLHTTPCodecStats

TLHTTPCodecStats::TLHTTPCodecStats(
    const std::string& prefix,
    folly::Optional<CodecProtocol> protocol,
    folly::Optional<TransportDirection> direction)
    : prefix_(prefix),
      openConn_(prefix + "_conn.sum"),
      ingressSynStream_(prefix + "_ingress_syn_stream", SUM, RATE),
      ingressData_(prefix + "_ingress_data", SUM, RATE),
      ingressSettings_(prefix + "_ingress_settings", SUM, RATE),
      ingressGoaway_(prefix + "_ingress_goaway", SUM, RATE),
      ingressGoawayDrain_(prefix + "_ingress_goaway_drain", SUM, RATE),
      egressData_(prefix + "_egress_data", SUM, RATE),
      egressSettings_(prefix + "_egress_settings", SUM, RATE),
      egressGoaway_(prefix + "_egress_goaway", SUM, RATE),
      egressGoawayDrain_(prefix + "_egress_goaway_drain", SUM, RATE),
      egressPriority_(prefix + "_egress_priority", SUM, RATE) {
  // HQ leaves RST_STREAM, PING and WINDOW_UPDATE to the QUIC transport, and
  // its GOAWAY carries no error code: HQ codecs report every ingress GOAWAY as
  // NO_ERROR, sessions only send NO_ERROR or (coro) PROTOCOL_ERROR, and only
  // clients send PRIORITY_UPDATE.
  const bool hq = protocol && isHQCodecProtocol(*protocol);
  // Downstream codecs reject PUSH_PROMISE and only ever parse requests and
  // send responses; upstream sessions only send requests and never push.
  const bool downstream = direction == TransportDirection::DOWNSTREAM;
  const bool upstream = direction == TransportDirection::UPSTREAM;
  if (!downstream) {
    ingressSynReply_.emplace(prefix + "_ingress_syn_reply", SUM, RATE);
    ingressPushPromise_.emplace(prefix + "_ingress_push_promise", SUM, RATE);
    egressSynStream_.emplace(prefix + "_egress_syn_stream", SUM, RATE);
  }
  if (!upstream) {
    egressSynReply_.emplace(prefix + "_egress_syn_reply", SUM, RATE);
    egressPushPromise_.emplace(prefix + "_egress_push_promise", SUM, RATE);
  }
  if (!(hq && upstream)) {
    ingressPriority_.emplace(prefix + "_ingress_priority", SUM, RATE);
  }
  if (!hq) {
    ingressRst_.emplace(prefix + "_ingress_rst", SUM, RATE);
    ingressPingRequest_.emplace(prefix + "_ingress_ping_request", SUM, RATE);
    ingressPingReply_.emplace(prefix + "_ingress_ping_reply", SUM, RATE);
    ingressWindowUpdate_.emplace(prefix + "_ingress_window_update", SUM, RATE);
    egressRst_.emplace(prefix + "_egress_rst", SUM, RATE);
    egressPingRequest_.emplace(prefix + "_egress_ping_request", SUM, RATE);
    egressPingReply_.emplace(prefix + "_egress_ping_reply", SUM, RATE);
    egressWindowUpdate_.emplace(prefix + "_egress_window_update", SUM, RATE);
  }
  ingressRstStatus_.resize(kErrorStrings.size());
  egressRstStatus_.resize(kErrorStrings.size());
  ingressGoawayStatus_.resize(kErrorStrings.size());
  egressGoawayStatus_.resize(kErrorStrings.size());
  for (size_t i = 0; i < kErrorStrings.size(); ++i) {
    const std::string errString = kErrorStrings[i];
    const auto code = ErrorCode(i);
    if (!hq) {
      ingressRstStatus_[i].emplace(
          folly::to<std::string>(prefix, "_ingress_rst_", errString), SUM);
      egressRstStatus_[i].emplace(
          folly::to<std::string>(prefix, "_egress_rst_", errString), SUM);
    }
    if (!hq || code == ErrorCode::NO_ERROR) {
      ingressGoawayStatus_[i].emplace(
          folly::to<std::string>(prefix, "_ingress_goaway_", errString), SUM);
    }
    if (!hq || code == ErrorCode::NO_ERROR ||
        code == ErrorCode::PROTOCOL_ERROR) {
      egressGoawayStatus_[i].emplace(
          folly::to<std::string>(prefix, "_egress_goaway_", errString), SUM);
    }
  }
}

void TLHTTPCodecStats::incrementParallelConn(int64_t amount) {
  openConn_.incrementValue(amount);
}
void TLHTTPCodecStats::recordIngressSynStream() {
  ingressSynStream_.add(1);
}
void TLHTTPCodecStats::recordIngressSynReply() {
  addOrLogSkipped(ingressSynReply_, prefix_, "ingress_syn_reply");
}
void TLHTTPCodecStats::recordIngressPushPromise() {
  addOrLogSkipped(ingressPushPromise_, prefix_, "ingress_push_promise");
}
void TLHTTPCodecStats::recordIngressData() {
  ingressData_.add(1);
}
void TLHTTPCodecStats::recordIngressRst(ErrorCode statusCode) {
  addOrLogSkipped(ingressRst_, prefix_, "ingress_rst");
  auto index = uint32_t(statusCode);
  if (index >= kErrorStrings.size()) {
    LOG(ERROR) << "Unknown ingress reset status code=" << index;
    index = (uint32_t)ErrorCode::PROTOCOL_ERROR;
  }
  addOrLogSkipped(
      ingressRstStatus_[index], prefix_, "ingress_rst_", kErrorStrings[index]);
}
void TLHTTPCodecStats::recordIngressSettings() {
  ingressSettings_.add(1);
}
void TLHTTPCodecStats::recordIngressPingRequest() {
  addOrLogSkipped(ingressPingRequest_, prefix_, "ingress_ping_request");
}
void TLHTTPCodecStats::recordIngressPingReply() {
  addOrLogSkipped(ingressPingReply_, prefix_, "ingress_ping_reply");
}
void TLHTTPCodecStats::recordIngressGoaway(ErrorCode statusCode) {
  ingressGoaway_.add(1);
  auto index = uint32_t(statusCode);
  if (index >= kErrorStrings.size()) {
    LOG(ERROR) << "Unknown ingress goaway status code=" << index;
    index = (uint32_t)ErrorCode::PROTOCOL_ERROR;
  }
  addOrLogSkipped(ingressGoawayStatus_[index],
                  prefix_,
                  "ingress_goaway_",
                  kErrorStrings[index]);
}
void TLHTTPCodecStats::recordIngressGoawayDrain() {
  ingressGoawayDrain_.add(1);
}
void TLHTTPCodecStats::recordIngressWindowUpdate() {
  addOrLogSkipped(ingressWindowUpdate_, prefix_, "ingress_window_update");
}
void TLHTTPCodecStats::recordIngressPriority() {
  addOrLogSkipped(ingressPriority_, prefix_, "ingress_priority");
}
void TLHTTPCodecStats::recordEgressSynStream() {
  addOrLogSkipped(egressSynStream_, prefix_, "egress_syn_stream");
}
void TLHTTPCodecStats::recordEgressSynReply() {
  addOrLogSkipped(egressSynReply_, prefix_, "egress_syn_reply");
}
void TLHTTPCodecStats::recordEgressPushPromise() {
  addOrLogSkipped(egressPushPromise_, prefix_, "egress_push_promise");
}
void TLHTTPCodecStats::recordEgressData() {
  egressData_.add(1);
}
void TLHTTPCodecStats::recordEgressRst(ErrorCode statusCode) {
  addOrLogSkipped(egressRst_, prefix_, "egress_rst");
  auto index = uint32_t(statusCode);
  if (index >= kErrorStrings.size()) {
    LOG(ERROR) << "Unknown egress reset status code=" << index;
    index = (uint32_t)ErrorCode::PROTOCOL_ERROR;
  }
  addOrLogSkipped(
      egressRstStatus_[index], prefix_, "egress_rst_", kErrorStrings[index]);
}
void TLHTTPCodecStats::recordEgressSettings() {
  egressSettings_.add(1);
}
void TLHTTPCodecStats::recordEgressPingRequest() {
  addOrLogSkipped(egressPingRequest_, prefix_, "egress_ping_request");
}
void TLHTTPCodecStats::recordEgressPingReply() {
  addOrLogSkipped(egressPingReply_, prefix_, "egress_ping_reply");
}
void TLHTTPCodecStats::recordEgressGoaway(ErrorCode statusCode) {
  egressGoaway_.add(1);
  auto index = uint32_t(statusCode);
  if (index >= kErrorStrings.size()) {
    LOG(ERROR) << "Unknown egress goaway status code=" << index;
    index = (uint32_t)ErrorCode::PROTOCOL_ERROR;
  }
  addOrLogSkipped(egressGoawayStatus_[index],
                  prefix_,
                  "egress_goaway_",
                  kErrorStrings[index]);
}
void TLHTTPCodecStats::recordEgressGoawayDrain() {
  egressGoawayDrain_.add(1);
}
void TLHTTPCodecStats::recordEgressWindowUpdate() {
  addOrLogSkipped(egressWindowUpdate_, prefix_, "egress_window_update");
}
void TLHTTPCodecStats::recordEgressPriority() {
  egressPriority_.add(1);
}

} // namespace proxygen
