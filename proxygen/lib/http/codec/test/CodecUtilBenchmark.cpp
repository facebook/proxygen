/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>
#include <glog/logging.h>
#include <proxygen/lib/http/codec/CodecUtil.h>
#include <string>
#include <string_view>

using namespace proxygen;

namespace {

constexpr std::string_view kShortValue = "gzip, deflate, br";

constexpr std::string_view kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

constexpr std::string_view kFoldedValue =
    "text/html,\r\n application/xhtml+xml,\r\n\t*/*";

constexpr std::string_view kRejectsImmediately =
    "\x01"
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

const std::string& cookie() {
  static const std::string v = [] {
    std::string s;
    for (int i = 0; i < 40; i++) {
      s +=
          folly::to<std::string>("c", i, "=abcdef0123456789abcdef0123456789; ");
    }
    return s;
  }();
  return v;
}

void validate(unsigned iters,
              std::string_view value,
              CodecUtil::CtlEscapeMode mode) {
  folly::ByteRange range(folly::StringPiece{value});
  folly::makeUnpredictable(range);
  for (unsigned i = 0; i < iters; i++) {
    folly::doNotOptimizeAway(CodecUtil::validateHeaderValue(range, mode));
  }
}

} // namespace

BENCHMARK(ValidateShortValueStrict, iters) {
  validate(iters, kShortValue, CodecUtil::CtlEscapeMode::STRICT);
}

BENCHMARK(ValidateUserAgentStrict, iters) {
  validate(iters, kUserAgent, CodecUtil::CtlEscapeMode::STRICT);
}

BENCHMARK(ValidateUserAgentStrictCompat, iters) {
  validate(iters, kUserAgent, CodecUtil::CtlEscapeMode::STRICT_COMPAT);
}

BENCHMARK(ValidateCookieStrict, iters) {
  validate(iters, cookie(), CodecUtil::CtlEscapeMode::STRICT);
}

BENCHMARK(ValidateFoldedValueStrictCompat, iters) {
  validate(iters, kFoldedValue, CodecUtil::CtlEscapeMode::STRICT_COMPAT);
}

BENCHMARK(ValidateRejectsImmediatelyStrict, iters) {
  validate(iters, kRejectsImmediately, CodecUtil::CtlEscapeMode::STRICT);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  folly::runBenchmarks();
  return 0;
}
