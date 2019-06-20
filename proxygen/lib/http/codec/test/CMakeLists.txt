# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

if(NOT BUILD_TESTING)
    return()
endif()

add_library(codectestutils STATIC TestUtils.cpp)
target_include_directories(
    codectestutils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    ${LIBGMOCK_INCLUDE_DIR}
    ${LIBGTEST_INCLUDE_DIR}
)
target_link_libraries(codectestutils PRIVATE proxygen)

proxygen_add_test(TARGET CodecTests
  SOURCES
    DefaultHTTPCodecFactoryTest.cpp
    FilterTests.cpp
    HTTP1xCodecTest.cpp
    HTTP2CodecTest.cpp
    HTTP2FramerTest.cpp
    SPDYCodecTest.cpp
    ${HTTP3_CODEC_TESTS}
  DEPENDS
    codectestutils
    proxygen
    testmain
)

if (BUILD_QUIC)
  proxygen_add_test(TARGET HQFramerTests
    SOURCES
      HQFramerTest.cpp
    DEPENDS
      codectestutils
      proxygen
      testmain
      mvfst::mvfst_codec_types
  )

  proxygen_add_test(TARGET HQCodecTests
    SOURCES
      HQCodecTest.cpp
      UnframedBodyOffsetTrackerTest.cpp
    DEPENDS
      codectestutils
      proxygen
      testmain
      mvfst::mvfst_codec_types
  )
endif()
