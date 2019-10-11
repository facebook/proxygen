/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/http/stats/SPDYStats.h"

using facebook::fb303::RATE;
using facebook::fb303::SUM;

namespace {
const uint32_t kRstStatusCount = 12;
static const char* kRstStatusStrings[] = {"Ok",
                                          "Protocol_Error",
                                          "Invalid_Stream",
                                          "Refused_Stream",
                                          "Unsupported_Version",
                                          "Cancel",
                                          "Internal_Error",
                                          "Flow_Control_Error",
                                          "Stream_In_Use",
                                          "Stream_Already_Closed",
                                          "Invalid_Credentials",
                                          "Frame_Too_Large"};

const uint32_t kGoawayStatusCount = 4;
static const char* kGoawayStatusStrings[] = {
    "Ok", "Protocol_Error", "Internal_Error", "Flow_Control_Error"};

} // namespace

namespace proxygen {

// TLSPDYStats

TLSPDYStats::TLSPDYStats(const std::string& prefix)
    : openConn_(prefix + "_conn.sum"),
      ingressSynStream_(prefix + "_ingress_syn_stream", SUM, RATE),
      ingressSynReply_(prefix + "_ingress_syn_reply", SUM, RATE),
      ingressData_(prefix + "_ingress_data", SUM, RATE),
      ingressRst_(prefix + "_ingress_rst", SUM, RATE),
      ingressSettings_(prefix + "_ingress_settings", SUM, RATE),
      ingressPingRequest_(prefix + "_ingress_ping_request", SUM, RATE),
      ingressPingReply_(prefix + "_ingress_ping_reply", SUM, RATE),
      ingressGoaway_(prefix + "_ingress_goaway", SUM, RATE),
      ingressGoawayDrain_(prefix + "_ingress_goaway_drain", SUM, RATE),
      ingressWindowUpdate_(prefix + "_ingress_window_update", SUM, RATE),
      ingressPriority_(prefix + "_ingress_priority", SUM, RATE),
      egressSynStream_(prefix + "_egress_syn_stream", SUM, RATE),
      egressSynReply_(prefix + "_egress_syn_reply", SUM, RATE),
      egressData_(prefix + "_egress_data", SUM, RATE),
      egressRst_(prefix + "_egress_rst", SUM, RATE),
      egressSettings_(prefix + "_egress_settings", SUM, RATE),
      egressPingRequest_(prefix + "_egress_ping_request", SUM, RATE),
      egressPingReply_(prefix + "_egress_ping_reply", SUM, RATE),
      egressGoaway_(prefix + "_egress_goaway", SUM, RATE),
      egressGoawayDrain_(prefix + "_egress_goaway_drain", SUM, RATE),
      egressWindowUpdate_(prefix + "_egress_window_update", SUM, RATE),
      egressPriority_(prefix + "_egress_priority", SUM, RATE) {
  ingressRstStatus_.reserve(kRstStatusCount);
  egressRstStatus_.reserve(kRstStatusCount);
  ingressGoawayStatus_.reserve(kGoawayStatusCount);
  egressGoawayStatus_.reserve(kGoawayStatusCount);
  for (uint32_t i = 0; i < kRstStatusCount; ++i) {
    ingressRstStatus_.emplace_back(
        prefix + "_ingress_rst_" + kRstStatusStrings[i], SUM);
    egressRstStatus_.emplace_back(
        prefix + "_egress_rst_" + kRstStatusStrings[i], SUM);
  }
  for (uint32_t i = 0; i < kGoawayStatusCount; ++i) {
    ingressGoawayStatus_.emplace_back(
        prefix + "_ingress_goaway_" + kGoawayStatusStrings[i], SUM);
    egressGoawayStatus_.emplace_back(
        prefix + "_egress_goaway_" + kGoawayStatusStrings[i], SUM);
  }
}

void TLSPDYStats::incrementSpdyConn(int64_t amount) {
  openConn_.incrementValue(amount);
}
void TLSPDYStats::recordIngressSynStream() {
  ingressSynStream_.add(1);
}
void TLSPDYStats::recordIngressSynReply() {
  ingressSynReply_.add(1);
}
void TLSPDYStats::recordIngressData() {
  ingressData_.add(1);
}
void TLSPDYStats::recordIngressRst(uint32_t statusCode) {
  ingressRst_.add(1);
  if (statusCode >= kRstStatusCount) {
    LOG(ERROR) << "Invalid SPDY ingress reset status code=" << statusCode;
    statusCode = 1; // protocol error
  }
  ingressRstStatus_[statusCode].add(1);
}
void TLSPDYStats::recordIngressSettings() {
  ingressSettings_.add(1);
}
void TLSPDYStats::recordIngressPingRequest() {
  ingressPingRequest_.add(1);
}
void TLSPDYStats::recordIngressPingReply() {
  ingressPingReply_.add(1);
}
void TLSPDYStats::recordIngressGoaway(spdy::GoawayStatusCode statusCode) {
  ingressGoaway_.add(1);
  uint32_t index = statusCode;
  if (statusCode == spdy::GOAWAY_FLOW_CONTROL_ERROR) {
    index = 3;
  }
  if (index >= kGoawayStatusCount) {
    LOG(ERROR) << "Invalid SPDY ingress goaway status code=" << statusCode;
    index = 1; // protocol error
  }
  ingressGoawayStatus_[index].add(1);
}
void TLSPDYStats::recordIngressGoawayDrain() {
  ingressGoawayDrain_.add(1);
}
void TLSPDYStats::recordIngressWindowUpdate() {
  ingressWindowUpdate_.add(1);
}
void TLSPDYStats::recordIngressPriority() {
  ingressPriority_.add(1);
}
void TLSPDYStats::recordEgressSynStream() {
  egressSynStream_.add(1);
}
void TLSPDYStats::recordEgressSynReply() {
  egressSynReply_.add(1);
}
void TLSPDYStats::recordEgressData() {
  egressData_.add(1);
}
void TLSPDYStats::recordEgressRst(uint32_t statusCode) {
  egressRst_.add(1);
  if (statusCode >= kRstStatusCount) {
    LOG(ERROR) << "Invalid SPDY egress reset status code=" << statusCode;
    statusCode = 1; // protocol error
  }
  egressRstStatus_[statusCode].add(1);
}
void TLSPDYStats::recordEgressSettings() {
  egressSettings_.add(1);
}
void TLSPDYStats::recordEgressPingRequest() {
  egressPingRequest_.add(1);
}
void TLSPDYStats::recordEgressPingReply() {
  egressPingReply_.add(1);
}
void TLSPDYStats::recordEgressGoaway(spdy::GoawayStatusCode statusCode) {
  egressGoaway_.add(1);
  uint32_t index = statusCode;
  if (statusCode == spdy::GOAWAY_FLOW_CONTROL_ERROR) {
    index = 3;
  }
  if (index >= kGoawayStatusCount) {
    LOG(ERROR) << "Invalid SPDY egress goaway status code=" << statusCode;
    index = 1; // protocol error
  }
  egressGoawayStatus_[index].add(1);
}
void TLSPDYStats::recordEgressGoawayDrain() {
  egressGoawayDrain_.add(1);
}
void TLSPDYStats::recordEgressWindowUpdate() {
  egressWindowUpdate_.add(1);
}
void TLSPDYStats::recordEgressPriority() {
  egressPriority_.add(1);
}

} // namespace proxygen
