# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

if(NOT BUILD_TESTS)
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
      HQByteEventTrackerTest.cpp
      HQDownstreamSessionTest.cpp
      HQSessionMocksTest.cpp
      HQSessionTestCommon.cpp
      HQStreamBaseTest.cpp
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
