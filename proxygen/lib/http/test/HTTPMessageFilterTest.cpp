/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/HTTPMessageFilters.h>
#include <proxygen/lib/http/test/MockHTTPMessageFilter.h>
#include <proxygen/lib/http/session/test/HTTPTransactionMocks.h>

using namespace proxygen;
using namespace std;

class TestFilter : public HTTPMessageFilter {
  std::unique_ptr<HTTPMessageFilter> clone () noexcept override {
    return nullptr;
  }
};

TEST(HTTPMessageFilter, TestFilterPauseResumePropagatedToFilter) {
  //              prev               prev
  // testFilter2 -----> testFilter1 -----> mockFilter

  TestFilter testFilter1;
  TestFilter testFilter2;
  MockHTTPMessageFilter mockFilter;

  testFilter2.setPrevFilter(&testFilter1);
  testFilter1.setPrevFilter(&mockFilter);

  EXPECT_CALL(mockFilter, pause());
  testFilter2.pause();

  EXPECT_CALL(mockFilter, resume(10));
  testFilter2.resume(10);
}

TEST(HTTPMessageFilter, TestFilterPauseResumePropagatedToTxn) {
  //              prev               prev
  // testFilter2 -----> testFilter1 -----> mockFilter

  TestFilter testFilter1;
  TestFilter testFilter2;

  HTTP2PriorityQueue q;
  MockHTTPTransaction mockTxn(TransportDirection::UPSTREAM, 1, 0, q);

  testFilter2.setPrevFilter(&testFilter1);
  testFilter1.setPrevTxn(&mockTxn);

  EXPECT_CALL(mockTxn, pauseIngress());
  testFilter2.pause();

  EXPECT_CALL(mockTxn, resumeIngress());
  testFilter2.resume(10);
}
