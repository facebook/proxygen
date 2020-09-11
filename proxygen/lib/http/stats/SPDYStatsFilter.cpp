/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/http/stats/SPDYStatsFilter.h"

#include "proxygen/lib/http/stats/SPDYStats.h"

namespace proxygen {

SPDYStatsFilter::SPDYStatsFilter(SPDYStats* counters, CodecProtocol protocol)
    : counters_(counters), protocol_(protocol) {
  counters_->incrementSpdyConn(1);
}

SPDYStatsFilter::~SPDYStatsFilter() {
  counters_->incrementSpdyConn(-1);
}

void SPDYStatsFilter::onHeadersComplete(StreamID stream,
                                        std::unique_ptr<HTTPMessage> msg) {
  if (call_->getTransportDirection() == TransportDirection::DOWNSTREAM ||
      (stream % 2 == 0 && msg->isRequest())) {
    counters_->recordIngressSynStream();
  } else {
    counters_->recordIngressSynReply();
  }
  callback_->onHeadersComplete(stream, std::move(msg));
}

void SPDYStatsFilter::onBody(StreamID stream,
                             std::unique_ptr<folly::IOBuf> chain,
                             uint16_t padding) {
  counters_->recordIngressData();
  callback_->onBody(stream, std::move(chain), padding);
}

void SPDYStatsFilter::onAbort(StreamID stream, ErrorCode statusCode) {
  if (stream) {
    counters_->recordIngressRst(spdy::errorCodeToReset(statusCode));
  } else {
    counters_->recordIngressGoaway(spdy::errorCodeToGoaway(statusCode));
  }
  callback_->onAbort(stream, statusCode);
}

void SPDYStatsFilter::onGoaway(uint64_t lastGoodStreamID,
                               ErrorCode statusCode,
                               std::unique_ptr<folly::IOBuf> debugData) {
  counters_->recordIngressGoaway(spdy::errorCodeToGoaway(statusCode));
  if (lastGoodStreamID == std::numeric_limits<int32_t>::max()) {
    counters_->recordIngressGoawayDrain();
  }
  callback_->onGoaway(lastGoodStreamID, statusCode, std::move(debugData));
}

void SPDYStatsFilter::onPingRequest(uint64_t data) {
  counters_->recordIngressPingRequest();
  callback_->onPingRequest(data);
}

void SPDYStatsFilter::onPingReply(uint64_t data) {
  counters_->recordIngressPingReply();
  callback_->onPingReply(data);
}

void SPDYStatsFilter::onWindowUpdate(StreamID stream, uint32_t amount) {
  counters_->recordIngressWindowUpdate();
  callback_->onWindowUpdate(stream, amount);
}

void SPDYStatsFilter::onSettings(const SettingsList& settings) {
  counters_->recordIngressSettings();
  callback_->onSettings(settings);
}

void SPDYStatsFilter::onSettingsAck() {
  counters_->recordIngressSettings();
  callback_->onSettingsAck();
}

void SPDYStatsFilter::onPriority(StreamID stream,
                                 const HTTPMessage::HTTPPriority& pri) {
  counters_->recordIngressPriority();
  callback_->onPriority(stream, pri);
}

void SPDYStatsFilter::generateHeader(folly::IOBufQueue& writeBuf,
                                     StreamID stream,
                                     const HTTPMessage& msg,
                                     bool eom,
                                     HTTPHeaderSize* size) {
  if (call_->getTransportDirection() == TransportDirection::UPSTREAM) {
    counters_->recordEgressSynStream();
  } else {
    counters_->recordEgressSynReply();
  }
  return call_->generateHeader(writeBuf, stream, msg, eom, size);
}

void SPDYStatsFilter::generatePushPromise(folly::IOBufQueue& writeBuf,
                                          StreamID stream,
                                          const HTTPMessage& msg,
                                          StreamID assocStream,
                                          bool eom,
                                          HTTPHeaderSize* size) {
  counters_->recordEgressSynStream();
  return call_->generatePushPromise(
      writeBuf, stream, msg, assocStream, eom, size);
}

size_t SPDYStatsFilter::generateBody(folly::IOBufQueue& writeBuf,
                                     StreamID stream,
                                     std::unique_ptr<folly::IOBuf> chain,
                                     folly::Optional<uint8_t> padding,
                                     bool eom) {
  counters_->recordEgressData();
  return call_->generateBody(writeBuf, stream, std::move(chain), padding, eom);
}

size_t SPDYStatsFilter::generateRstStream(folly::IOBufQueue& writeBuf,
                                          StreamID stream,
                                          ErrorCode statusCode) {
  counters_->recordEgressRst(spdy::errorCodeToReset(statusCode));
  return call_->generateRstStream(writeBuf, stream, statusCode);
}

size_t SPDYStatsFilter::generateGoaway(
    folly::IOBufQueue& writeBuf,
    StreamID lastStream,
    ErrorCode statusCode,
    std::unique_ptr<folly::IOBuf> debugData) {
  auto written = call_->generateGoaway(
      writeBuf, lastStream, statusCode, std::move(debugData));
  if (written) {
    counters_->recordEgressGoaway(spdy::errorCodeToGoaway(statusCode));
    if (lastStream == std::numeric_limits<int32_t>::max()) {
      counters_->recordEgressGoawayDrain();
    }
  }
  return written;
}

size_t SPDYStatsFilter::generatePingRequest(folly::IOBufQueue& writeBuf,
                                            folly::Optional<uint64_t> data) {
  counters_->recordEgressPingRequest();
  return call_->generatePingRequest(writeBuf, data);
}

size_t SPDYStatsFilter::generatePingReply(folly::IOBufQueue& writeBuf,
                                          uint64_t data) {
  counters_->recordEgressPingReply();
  return call_->generatePingReply(writeBuf, data);
}

size_t SPDYStatsFilter::generateSettings(folly::IOBufQueue& buf) {
  counters_->recordEgressSettings();
  return call_->generateSettings(buf);
}

size_t SPDYStatsFilter::generateWindowUpdate(folly::IOBufQueue& writeBuf,
                                             StreamID stream,
                                             uint32_t delta) {
  counters_->recordEgressWindowUpdate();
  return call_->generateWindowUpdate(writeBuf, stream, delta);
}

size_t SPDYStatsFilter::generatePriority(folly::IOBufQueue& writeBuf,
                                         StreamID stream,
                                         const HTTPMessage::HTTPPriority& pri) {
  counters_->recordEgressPriority();
  return call_->generatePriority(writeBuf, stream, pri);
}

} // namespace proxygen
