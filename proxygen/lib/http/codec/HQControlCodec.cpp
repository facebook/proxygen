/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HQControlCodec.h>
#include <proxygen/lib/http/HTTP3ErrorCode.h>
#include <proxygen/lib/http/codec/HQUtils.h>

#include <folly/Random.h>

namespace proxygen { namespace hq {

using namespace folly::io;

ParseResult HQControlCodec::checkFrameAllowed(FrameType type) {
  switch (type) {
    case hq::FrameType::DATA:
    case hq::FrameType::HEADERS:
    case hq::FrameType::PUSH_PROMISE:
      return HTTP3::ErrorCode::HTTP_WRONG_STREAM;
    default:
      break;
  }

  if (getStreamType() == hq::UnidirectionalStreamType::CONTROL) {
    // SETTINGS MUST be the first frame on an HQ Control Stream
    if (!receivedSettings_ && type != hq::FrameType::SETTINGS) {
      return HTTP3::ErrorCode::HTTP_MISSING_SETTINGS;
    }
    // multiple SETTINGS frames are not allowed
    if (receivedSettings_ && type == hq::FrameType::SETTINGS) {
      return HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME;
    }
    // A server MUST treat receipt of a GOAWAY frame as a connection error
    // of type HTTP_UNEXPECTED_FRAME
    if (transportDirection_ == TransportDirection::DOWNSTREAM &&
        type == hq::FrameType::GOAWAY) {
      return HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME;
    }
    // A client MUST treat the receipt of a MAX_PUSH_ID frame as a connection
    // error of type HTTP_UNEXPECTED_FRAME
    if (transportDirection_ == TransportDirection::UPSTREAM &&
        type == hq::FrameType::MAX_PUSH_ID) {
      return HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME;
    }
  }

  // Only GOAWAY from Server to Client are allowed in H1Q
  if (getStreamType() == hq::UnidirectionalStreamType::H1Q_CONTROL &&
      (transportDirection_ == TransportDirection::DOWNSTREAM ||
       type != hq::FrameType::GOAWAY)) {
    return HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME;
  }

  return folly::none;
}

ParseResult HQControlCodec::parseCancelPush(Cursor& cursor,
                                            const FrameHeader& header) {
  PushId outPushId;
  auto res = hq::parseCancelPush(cursor, header, outPushId);
  return res;
}

ParseResult HQControlCodec::parseSettings(Cursor& cursor,
                                          const FrameHeader& header) {
  CHECK(isIngress());
  std::deque<SettingPair> outSettings;
  receivedSettings_ = true;
  auto res = hq::parseSettings(cursor, header, outSettings);
  if (res) {
    return res;
  }

  SettingsList settingsList;
  for (auto& setting : outSettings) {
    switch (setting.first) {
      case hq::SettingId::HEADER_TABLE_SIZE:
      case hq::SettingId::MAX_HEADER_LIST_SIZE:
      case hq::SettingId::QPACK_BLOCKED_STREAMS:
        break;
      default:
        continue; // ignore unknown settings
    }
    auto httpSettingId = hqToHttpSettingsId(setting.first);
    settings_.setSetting(*httpSettingId, setting.second);
    settingsList.push_back(*settings_.getSetting(*httpSettingId));
  }

  if (callback_) {
    callback_->onSettings(settingsList);
  }
  return folly::none;
}

ParseResult HQControlCodec::parseGoaway(Cursor& cursor,
                                        const FrameHeader& header) {
  quic::StreamId outStreamId;
  auto res = hq::parseGoaway(cursor, header, outStreamId);
  if (!res && callback_) {
    callback_->onGoaway(outStreamId, ErrorCode::NO_ERROR);
  }
  return res;
}

ParseResult HQControlCodec::parseMaxPushId(Cursor& cursor,
                                           const FrameHeader& header) {
  quic::StreamId outPushId;
  auto res = hq::parseMaxPushId(cursor, header, outPushId);
  return res;
}

bool HQControlCodec::isWaitingToDrain() const {
  return sentGoaway_;
}

size_t HQControlCodec::generateGoaway(
    folly::IOBufQueue& writeBuf,
    StreamID lastStream,
    ErrorCode /*statusCode*/,
    std::unique_ptr<folly::IOBuf> /*debugData*/) {
  DCHECK_GE(maxSeenLastStream_, lastStream);
  maxSeenLastStream_ = lastStream;
  auto writeRes = hq::writeGoaway(writeBuf, lastStream);
  if (writeRes.hasError()) {
    LOG(FATAL) << "error writing goaway with streamID=" << lastStream;
    return 0;
  }
  sentGoaway_ = true;
  return *writeRes;
}

size_t HQControlCodec::generateSettings(folly::IOBufQueue& writeBuf) {
  CHECK(isEgress());
  CHECK(!sentSettings_);
  sentSettings_ = true;
  std::deque<hq::SettingPair> settings;
  for (auto& setting : settings_.getAllSettings()) {
    auto id = httpToHqSettingsId(setting.id);
    // unknown ids will return folly::none
    if (id) {
      switch (*id) {
        case hq::SettingId::HEADER_TABLE_SIZE:
        case hq::SettingId::MAX_HEADER_LIST_SIZE:
        case hq::SettingId::QPACK_BLOCKED_STREAMS:
          break;
      }
      settings.emplace_back(*id, (SettingValue)setting.value);
    }
  }
  // add a random setting from the greasing pool
  settings.emplace_back(
      static_cast<SettingId>(*getGreaseId(folly::Random::rand32(16))),
      static_cast<SettingValue>(0xFACEB00C));
  auto writeRes = writeSettings(writeBuf, settings);
  if (writeRes.hasError()) {
    LOG(FATAL) << "error writing settings frame";
    return 0;
  }
  sentSettings_ = true;
  return *writeRes;
}

size_t HQControlCodec::generatePriority(
    folly::IOBufQueue& /*writeBuf*/,
    StreamID /*stream*/,
    const HTTPMessage::HTTPPriority& /*pri*/) {
  CHECK(false) << __func__ << "not implemented yet";
  return 0;
}

size_t HQControlCodec::addPriorityNodes(PriorityQueue& /*queue*/,
                                        folly::IOBufQueue& /*writeBuf*/,
                                        uint8_t /*maxLevel*/) {

  CHECK(false) << __func__ << " not implemented";
  return 0;
}

HTTPCodec::StreamID HQControlCodec::mapPriorityToDependency(
    uint8_t /*priority*/) const {
  CHECK(false) << __func__ << " not implemented";
  return 0;
}

}} // namespace proxygen::hq
