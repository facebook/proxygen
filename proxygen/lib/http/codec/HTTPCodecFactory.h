/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/TransportDirection.h>

namespace proxygen {

/**
 * Factory for produces HTTPCodec objects.
 */
class HTTPCodecFactory {
 public:
  explicit HTTPCodecFactory() {
  }
  virtual ~HTTPCodecFactory() {
  }

  /**
   * Get a codec instance
   */
  virtual std::unique_ptr<HTTPCodec> getCodec(const std::string& protocolHint,
                                              TransportDirection direction,
                                              bool isTLS) = 0;

  static std::unique_ptr<HTTPCodec> getCodec(CodecProtocol protocol,
                                             TransportDirection direction);
};

} // namespace proxygen
