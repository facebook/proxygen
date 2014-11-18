/*
 *  Copyright (c) 2014, Facebook, Inc.
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
 * protocol we need to change this version so that old clients will not be
 * able to negotiate it anymore.
 */
const uint8_t kHPACKMajorVersion = 0; // while is still in draft
const uint8_t kHPACKMinorVersion = 5; // based on HPACK draft 5
extern const std::string kHpackNpn; // NPN string for SPDY w/ HPACK

class HPACKCodec : public HeaderCodec {
 public:
  explicit HPACKCodec(TransportDirection direction);
  virtual ~HPACKCodec() {}

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept override;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

 protected:
  std::unique_ptr<HPACKEncoder> encoder_;
  std::unique_ptr<HPACKDecoder> decoder_;

 private:
  std::vector<HPACKHeader> decodedHeaders_;
};

}
