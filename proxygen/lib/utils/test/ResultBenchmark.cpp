/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Benchmark.h>
#include <proxygen/lib/utils/Result.h>
#include <type_traits>

using namespace folly;
using namespace proxygen;

struct MediumStruct {
  int32_t field1;
  int32_t field2;
};

typedef Result<MediumStruct, int> MediumResult;

static_assert(std::is_nothrow_copy_constructible<MediumResult>::value, "");
static_assert(sizeof(MediumStruct) == 8, "");

MediumResult __attribute__ ((__noinline__))
parseMediumResult(bool which) {
  if (which) {
    return MediumStruct{1, 2};
  } else {
    return 0;
  }
}

uint8_t __attribute__ ((__noinline__))
parseMediumReference(bool which, MediumStruct& outMedium) {
  if (which) {
    outMedium.field1 = 1;
    outMedium.field2 = 2;
    return 1;
  } else {
    return 0;
  }
}

BENCHMARK(result_medium_struct_or_byte, numIters) {
  const auto halfIters = numIters / 2;
  MediumResult result = 2;
  bool succeed = false;
  for (unsigned i = 0; i < numIters; ++i) {
    result = parseMediumResult(i < halfIters);
    succeed = result.isOk();
  }
  VLOG(4) << succeed << result.isError();
}

BENCHMARK(reference_medium_struct_or_byte, numIters) {
  const auto halfIters = numIters / 2;
  MediumStruct result;
  bool succeed = false;
  for (unsigned i = 0; i < numIters; ++i) {
    auto res = parseMediumReference(i < halfIters, result);
    succeed = res != 0;
  }
  VLOG(4) << succeed << result.field1;
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
