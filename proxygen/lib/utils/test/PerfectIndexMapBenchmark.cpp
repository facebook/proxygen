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
#include <proxygen/lib/utils/PerfectIndexMap.h>
#include <unordered_map>

using namespace folly;
using namespace proxygen;

// buck build @mode/opt proxygen/lib/utils/test:perfect_index_map_benchmark
// ./buck-out/gen/proxygen/lib/utils/test/perfect_index_map_benchmark -bm_min_iters 1000
// ============================================================================
// proxygen/lib/utils/test/PerfectIndexMapBenchmark.cpprelative  time/iter  iters/s
// ============================================================================
// proxygen/lib/utils/test/PerfectIndexMapBenchmark.cpprelative  time/iter  iters/s
// ============================================================================
// UnorderedMapUniqueInserts                                    8.80us  113.62K
// UnorderedMapUniqueGets                                       9.95us  100.49K
// PerfectIndexMapUniqueInsertsCode                             3.12us  320.90K
// PerfectIndexMapUniqueInsertsHashCodeString                   7.35us  136.09K
// PerfectIndexMapUniqueInsertsHashOtherString                 37.07us   26.98K
// PerfectIndexMapUniqueGetsCode                                4.71us  212.23K
// PerfectIndexMapUniqueGetsCodeString                          8.97us  111.49K
// PerfectIndexMapUniqueGetsOtherString                        36.23us   27.60K
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

std::vector<const std::string *> getTestHeaderCodeStrings() {
  std::vector<const std::string *> testHeadersCodeStrings;
  for (uint64_t j = HTTPHeaderCodeCommonOffset;
       j < HTTPCommonHeaders::num_header_codes; ++j) {
    testHeadersCodeStrings.push_back(
      HTTPCommonHeaders::getPointerToHeaderName(
        static_cast<HTTPHeaderCode>(j)));
  }
  return testHeadersCodeStrings;
}

std::vector<const std::string *> getTestHeaderOtherStrings() {
  std::vector<const std::string *> testHeadersOtherStrings;
  for (uint64_t j = HTTPHeaderCodeCommonOffset;
       j < HTTPCommonHeaders::num_header_codes; ++j) {
    testHeadersOtherStrings.push_back(
      new std::string(
        *HTTPCommonHeaders::getPointerToHeaderName(
          static_cast<HTTPHeaderCode>(j))
        + "0"));
  }
  return testHeadersOtherStrings;
}

static const std::vector<HTTPHeaderCode> testHeaderCodes = getTestHeaderCodes();

static const std::vector<const std::string *> testHeadersCodeStrings =
  getTestHeaderCodeStrings();

static const std::vector<const std::string *> testHeadersOtherStrings =
  getTestHeaderOtherStrings();

typedef PerfectIndexMap<
    HTTPHeaderCode,
    HTTP_HEADER_OTHER,
    HTTP_HEADER_NONE,
    HTTPCommonHeaders::hash,
    false,
    false>
  DefaultPerfectIndexMap;

}

void UnorderedMapInsertBench(
    std::unordered_map<std::string,std::string>& testMap,
    const std::vector<const std::string *>& keysAndValues, int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& keyAndValue: keysAndValues) {
      // Modeled after old impl of varstore
      testMap[*keyAndValue] = *keyAndValue;
    }
  }
}

void PerfectIndexMapInsertCodeBench(
    DefaultPerfectIndexMap &map, const std::vector<HTTPHeaderCode>& keys,
    const std::vector<const std::string *>& values, int iters) {
  for (int i = 0; i < iters; ++i) {
    for (unsigned long j = 0; j < keys.size(); ++j) {
      map.set(keys[j], *values[j]);
    }
  }
}

void PerfectIndexMapInsertHashBench(
    DefaultPerfectIndexMap &map,
    const std::vector<const std::string *>& keysAndValues, int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& keyAndValue: keysAndValues) {
      map.set(*keyAndValue, *keyAndValue);
    }
  }
}

void UnorderedMapGetBench(
    std::unordered_map<std::string,std::string>& testMap,
    const std::vector<const std::string *>& keys, int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& key: keys) {
      // Modeled after old impl of varstore
      auto it = testMap.find(*key);
      folly::Optional<std::string> result = (
        it == testMap.end() ?
        folly::none : (folly::Optional<std::string>)it->second);
      CHECK(result != folly::none);
    }
  }
}

void PerfectIndexMapGetCodeBench(
    DefaultPerfectIndexMap &map, const std::vector<HTTPHeaderCode>& keys,
    int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& key: keys) {
      CHECK(map.getSingleOrNone(key) != folly::none);
    }
  }
}

void PerfectIndexMapGetStringBench(
    DefaultPerfectIndexMap &map, const std::vector<const std::string *>& keys,
    int iters) {
  for (int i = 0; i < iters; ++i) {
    for (auto const& key: keys) {
      CHECK(map.getSingleOrNone(*key) != folly::none);
    }
  }
}

std::unordered_map<std::string, std::string> bUnorderedMapUniqueInsertsMap;
BENCHMARK(UnorderedMapUniqueInserts, iters) {
  UnorderedMapInsertBench(
    bUnorderedMapUniqueInsertsMap, testHeadersCodeStrings, iters);
}

std::unordered_map<std::string, std::string>
    getBenchUnorderedMapUniqueGetsTestMap() {
  std::unordered_map<std::string, std::string> testMap;
  UnorderedMapInsertBench(testMap, testHeadersCodeStrings, 1);
  return testMap;
}
std::unordered_map<std::string, std::string> bUnorderedMapUniqueGetsMap =
  getBenchUnorderedMapUniqueGetsTestMap();
BENCHMARK(UnorderedMapUniqueGets, iters) {
  UnorderedMapGetBench(
    bUnorderedMapUniqueGetsMap, testHeadersCodeStrings, iters);
}

DefaultPerfectIndexMap bPerfectIndexMapUniqueInsertsCodeMap;
BENCHMARK(PerfectIndexMapUniqueInsertsCode, iters) {
  PerfectIndexMapInsertCodeBench(
    bPerfectIndexMapUniqueInsertsCodeMap, testHeaderCodes,
    testHeadersCodeStrings, iters);
}

DefaultPerfectIndexMap
  bPerfectIndexMapUniqueInsertsHashCodeStringTestMap;
BENCHMARK(PerfectIndexMapUniqueInsertsHashCodeString, iters) {
  PerfectIndexMapInsertHashBench(
    bPerfectIndexMapUniqueInsertsHashCodeStringTestMap, testHeadersCodeStrings,
    iters);
}

DefaultPerfectIndexMap
  bPerfectIndexMapUniqueInsertsHashOtherStringTestMap;
BENCHMARK(PerfectIndexMapUniqueInsertsHashOtherString, iters) {
  PerfectIndexMapInsertHashBench(
    bPerfectIndexMapUniqueInsertsHashOtherStringTestMap,
    testHeadersOtherStrings, iters);
}

DefaultPerfectIndexMap getBenchPerfectIndexMapUniqueGetsCodeTestMap() {
  DefaultPerfectIndexMap testMap;
  PerfectIndexMapInsertCodeBench(
    testMap, testHeaderCodes, testHeadersCodeStrings, 1);
  return testMap;
}
DefaultPerfectIndexMap bPerfectIndexMapUniqueGetsCodeMap =
  getBenchPerfectIndexMapUniqueGetsCodeTestMap();
BENCHMARK(PerfectIndexMapUniqueGetsCode, iters) {
  PerfectIndexMapGetCodeBench(
    bPerfectIndexMapUniqueGetsCodeMap, testHeaderCodes, iters);
}

DefaultPerfectIndexMap bPerfectIndexMapUniqueGetsCodeStringMap =
  getBenchPerfectIndexMapUniqueGetsCodeTestMap();
BENCHMARK(PerfectIndexMapUniqueGetsCodeString, iters) {
  PerfectIndexMapGetStringBench(
    bPerfectIndexMapUniqueGetsCodeStringMap, testHeadersCodeStrings, iters);
}

DefaultPerfectIndexMap getBenchPerfectIndexMapUniqueGetsOtherStringTestMap() {
  DefaultPerfectIndexMap testMap;
  PerfectIndexMapInsertHashBench(testMap, testHeadersOtherStrings, 1);
  return testMap;
}
DefaultPerfectIndexMap bPerfectIndexMapUniqueGetsOtherStringMap =
  getBenchPerfectIndexMapUniqueGetsOtherStringTestMap();
BENCHMARK(PerfectIndexMapUniqueGetsOtherString, iters) {
  PerfectIndexMapGetStringBench(
    bPerfectIndexMapUniqueGetsOtherStringMap, testHeadersOtherStrings, iters);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();

  // Not explicitly required but lets free memory we specifically allocated.
  for (auto * testHeaderOtherString: testHeadersOtherStrings) {
    delete testHeaderOtherString;
  }

  return 0;
}
