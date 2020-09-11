/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GFlags.h>

#include <folly/init/Init.h>
#include <folly/ssl/Init.h>

#include <proxygen/httpserver/samples/hq/ConnIdLogger.h>
#include <proxygen/httpserver/samples/hq/HQClient.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <proxygen/httpserver/samples/hq/HQServer.h>
#include <proxygen/lib/transport/PersistentQuicPskCache.h>

using namespace quic::samples;

int main(int argc, char* argv[]) {
#if FOLLY_HAVE_LIBGFLAGS
  // Enable glog logging to stderr by default.
  gflags::SetCommandLineOptionWithMode(
      "logtostderr", "1", gflags::SET_FLAGS_DEFAULT);
#endif
  folly::init(&argc, &argv, false);
  folly::ssl::init();

  auto expectedParams = initializeParamsFromCmdline();
  if (expectedParams) {
    auto& params = expectedParams.value();
    // TODO: move sink to params
    proxygen::ConnIdLogSink sink(params);
    if (sink.isValid()) {
      AddLogSink(&sink);
    } else if (!params.logdir.empty()) {
      LOG(ERROR) << "Cannot open " << params.logdir;
    }

    switch (params.mode) {
      case HQMode::SERVER:
        startServer(params);
        break;
      case HQMode::CLIENT:
        startClient(params);
        break;
      default:
        LOG(ERROR) << "Unknown mode specified: ";
        return -1;
    }
    return 0;
  } else {
    for (auto& param : expectedParams.error()) {
      LOG(ERROR) << "Invalid param: " << param.name << " " << param.value << " "
                 << param.errorMsg;
    }
  }
}
