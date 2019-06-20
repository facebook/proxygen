# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

proxygen_add_test(TARGET SessionTests
  SOURCES
    ByteEventTrackerTest.cpp
    DownstreamTransactionTest.cpp
    HTTPDownstreamSessionTest.cpp
    HTTPSessionAcceptorTest.cpp
    HTTPUpstreamSessionTest.cpp
    MockCodecDownstreamTest.cpp
    HTTP2PriorityQueueTest.cpp
    HTTPDefaultSessionCodecFactoryTest.cpp
    HTTPTransactionSMTest.cpp
    TestUtils.cpp
  DEPENDS
    codectestutils
    testtransport
    proxygen
    testmain
)
