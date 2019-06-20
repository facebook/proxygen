# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

proxygen_add_test(TARGET StructuredHeadersTests
  SOURCES
    StructuredHeadersBufferTest.cpp
    StructuredHeadersDecoderTest.cpp
    StructuredHeadersEncoderTest.cpp
    StructuredHeadersStandardTest.cpp
    StructuredHeadersUtilitiesTest.cpp
  DEPENDS
    proxygen
    testmain
)
