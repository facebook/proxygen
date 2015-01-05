/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKCodec.h>

#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKDecoder.h>

namespace proxygen {

HPACKCodec09::HPACKCodec09(TransportDirection direction) :
    HPACKCodec(direction) {
  // the direction is not used in the latest spec
  encoder_ = folly::make_unique<HPACKEncoder09>();
  decoder_ = folly::make_unique<HPACKDecoder09>();
}

}
