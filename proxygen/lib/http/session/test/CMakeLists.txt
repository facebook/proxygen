# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

if(NOT BUILD_TESTING)
    return()
endif()

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

if (BUILD_QUIC)
  proxygen_add_test(TARGET HQSessionTests
    SOURCES
      HQDownstreamSessionTest.cpp
      HQSessionMocksTest.cpp
      HQSessionTestCommon.cpp
      HQStreamBaseTest.cpp
      HQStreamLookupTest.cpp
      HQUnidirectionalCallbacksTest.cpp
      HQUpstreamSessionTest.cpp
      TestUtils.cpp
    DEPENDS
      codectestutils
      testtransport
      proxygen
      testmain
      mvfst::mvfst_codec_types
      mvfst::mvfst_state_machine
  )
endif()


