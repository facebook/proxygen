/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <algorithm>
#include <folly/Benchmark.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>

using namespace folly;
using namespace proxygen;

// buck build @mode/opt proxygen/lib/http/test:http_headers_benchmark
// ./buck-out/gen/proxygen/lib/http/test/http_headers_benchmark -bm_min_iters 100
// ============================================================================
// proxygen/lib/http/test/HTTPHeadersBenchmark.cpp relative  time/iter  iters/s
// ============================================================================
// HTTPCommonHeadersHash                                        3.50us  285.82K
// HTTPCommonHeadersGetHeaderCodeFromTableCommonHeaderName    161.70ns    6.18M
// memchr                                                       1.02us  976.02K
// stdFind                                                      5.59us  178.94K
// ============================================================================

namespace {

std::vector<HTTPHeaderCode> getTestHeaderCodes() {
  std::vector<HTTPHeaderCode> testHeaderCodes;
  for (uint64_t j = HTTPHeaderCodeCommonOffset;
       j < HTTPCommonHeaders::num_header_codes; ++j) {
    testHeaderCodes.push_back(static_cast<HTTPHeaderCode>(j));
  }
  return testHeaderCodes;
}

std::vector<const std::string *> getTestHeaderStrings() {
  std::vector<const std::string *> testHeaderStrings;
  for (uint64_t j = HTTPHeaderCodeCommonOffset;
       j < HTTPCommonHeaders::num_header_codes; ++j) {
    testHeaderStrings.push_back(
      HTTPCommonHeaders::getPointerToHeaderName(
        static_cast<HTTPHeaderCode>(j)));
  }
  return testHeaderStrings;
}

static const std::string* testHeaderNames =
  HTTPCommonHeaders::getPointerToHeaderName(HTTP_HEADER_NONE);

static const std::vector<HTTPHeaderCode> testHeaderCodes = getTestHeaderCodes();

static const std::vector<const std::string *> testHeaderStrings =
  getTestHeaderStrings();

}

void HTTPCommonHeadersHashBench(int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& testHeaderString: testHeaderStrings) {
      HTTPCommonHeaders::hash(*testHeaderString);
    }
  }
}

void HTTPCommonHeadersGetHeaderCodeFromTableCommonHeaderNameBench(int iters) {
  for (int i = 0; i < iters; ++i) {
    for (uint64_t j = HTTPHeaderCodeCommonOffset;
         j < HTTPCommonHeaders::num_header_codes; ++j) {
      HTTPCommonHeaders::getHeaderCodeFromTableCommonHeaderName(
        &testHeaderNames[j], TABLE_CAMELCASE);
    }
  }
}

BENCHMARK(HTTPCommonHeadersHash, iters) {
  HTTPCommonHeadersHashBench(iters);
}

BENCHMARK(HTTPCommonHeadersGetHeaderCodeFromTableCommonHeaderName, iters) {
  HTTPCommonHeadersGetHeaderCodeFromTableCommonHeaderNameBench(iters);
}

void memchrBench(int iters) {
  for (int i = 0; i < iters; ++i) {
    for (uint64_t j = HTTPHeaderCodeCommonOffset;
         j < HTTPCommonHeaders::num_header_codes; ++j) {
      CHECK(
        memchr(
          (void*)testHeaderCodes.data(), static_cast<HTTPHeaderCode>(j),
          testHeaderCodes.size()) != nullptr);
    }
  }
}

void stdFindBench(int iters) {
  for (int i = 0; i < iters; ++i) {
    for (uint64_t j = HTTPHeaderCodeCommonOffset;
         j < HTTPCommonHeaders::num_header_codes; ++j) {
      auto address = HTTPCommonHeaders::getPointerToHeaderName(
        static_cast<HTTPHeaderCode>(j));
      CHECK(
        std::find(
          testHeaderStrings.begin(), testHeaderStrings.end(), address) !=
            testHeaderStrings.end());
    }
  }
}

BENCHMARK(memchr, iters) {
  memchrBench(iters);
}

BENCHMARK(stdFind, iters) {
  stdFindBench(iters);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
