# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

if(NOT BUILD_TESTING)
    return()
endif()

add_library(hpacktestutils STATIC TestUtil.cpp)
target_include_directories(
    hpacktestutils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(hpacktestutils PRIVATE proxygen)

proxygen_add_test(TARGET HPACKTests
  SOURCES
    HeaderPieceTests.cpp
    HeaderTableTests.cpp
    HPACKBufferTests.cpp
    HPACKCodecTests.cpp
    HPACKContextTests.cpp
    HPACKHeaderTests.cpp
    HuffmanTests.cpp
    LoggingTests.cpp
    QPACKCodecTests.cpp
    QPACKContextTests.cpp
    QPACKHeaderTableTests.cpp
    RFCExamplesTests.cpp
  DEPENDS
    hpacktestutils
    proxygen
    testmain
)
