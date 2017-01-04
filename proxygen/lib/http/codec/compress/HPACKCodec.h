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

#include <memory>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

class HPACKHeader;

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the NPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class HPACKCodec : public HeaderCodec, HeaderCodec::StreamingCallback {
 public:
  explicit HPACKCodec(TransportDirection direction);
  ~HPACKCodec() override {}

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept override;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

  // Callbacks that handle Codec-level stats and errors
  void onHeader(const std::string& name, const std::string& value) override;
  void onHeadersComplete() override;
  void onDecodeError(HeaderDecodeError decodeError) override;

  void decodeStreaming(
      folly::io::Cursor& cursor,
      uint32_t length,
      HeaderCodec::StreamingCallback* streamingCb) noexcept override;

  void setEncoderHeaderTableSize(uint32_t size) {
    encoder_.setHeaderTableSize(size);
  }

  void setDecoderHeaderTableMaxSize(uint32_t size) {
    decoder_.setHeaderTableMaxSize(size);
  }

  void describe(std::ostream& os) const;

 protected:
  HPACKEncoder encoder_;
  HPACKDecoder decoder_;

 private:
  std::vector<HPACKHeader> decodedHeaders_;
};

std::ostream& operator<<(std::ostream& os, const HPACKCodec& codec);
}
