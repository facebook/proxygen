/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/httpserver/samples/hq/HQLoggerHelper.h>

using namespace quic::samples;

HQLoggerHelper::HQLoggerHelper(const std::string& path,
                               bool pretty,
                               const std::string& vantagePoint)
    : quic::FileQLogger(quic::kHTTP3ProtocolType, vantagePoint),
      outputPath_(path),
      pretty_(pretty) {
}

HQLoggerHelper::~HQLoggerHelper() {
  try {
    outputLogsToFile(outputPath_, pretty_);
  } catch (...) {
  }
}
