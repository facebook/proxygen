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

#include <proxygen/lib/http/codec/HTTPCodecFilter.h>

namespace proxygen {

/**
 * This class enforces certain higher-level HTTP semantics. It does not enforce
 * conditions that require state to decide. That is, this class is stateless and
 * only examines the calls and callbacks that go through it.
 */

class HTTPChecks: public PassThroughHTTPCodecFilter {
 public:
  // HTTPCodec::Callback methods

  void onHeadersComplete(StreamID stream,
                         std::unique_ptr<HTTPMessage> msg) override;

  // HTTPCodec methods

  void generateHeader(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      const HTTPMessage& msg,
                      StreamID assocStream,
                      bool eom,
                      HTTPHeaderSize* sizeOut) override;
};

}
