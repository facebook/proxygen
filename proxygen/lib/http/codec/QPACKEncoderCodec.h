/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/HTTP3ErrorCode.h>
#include <proxygen/lib/http/codec/HQUnidirectionalCodec.h>
#include <proxygen/lib/http/codec/HQUtils.h>
#include <proxygen/lib/http/codec/compress/QPACKCodec.h>

namespace proxygen { namespace hq {

class QPACKEncoderCodec : public HQUnidirectionalCodec {

 public:
  QPACKEncoderCodec(QPACKCodec& qpackCodec, Callback& cb)
      : HQUnidirectionalCodec(UnidirectionalStreamType::QPACK_ENCODER,
                              StreamDirection::INGRESS),
        qpackCodec_(qpackCodec),
        callback_(cb) {
  }

  // HQUnidirectionalCodec API
  std::unique_ptr<folly::IOBuf> onUnidirectionalIngress(
      std::unique_ptr<folly::IOBuf> buf) override {
    auto err = qpackCodec_.decodeEncoderStream(std::move(buf));
    if (err != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "QPACK encoder stream decode error err=" << err;
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                       "Compression error on encoder stream");
      ex.setErrno(uint32_t(HTTP3::ErrorCode::HTTP_QPACK_ENCODER_STREAM_ERROR));
      callback_.onError(kSessionStreamId, ex, false);
    }
    return nullptr;
  }
  void onUnidirectionalIngressEOF() override {
    LOG(ERROR) << "Unexpected QPACK encoder stream EOF";
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                     "Encoder stream EOF");
    ex.setErrno(uint32_t(HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM));
    callback_.onError(kSessionStreamId, ex, false);
  }

 private:
  QPACKCodec& qpackCodec_;
  Callback& callback_;
};

}} // namespace proxygen::hq
