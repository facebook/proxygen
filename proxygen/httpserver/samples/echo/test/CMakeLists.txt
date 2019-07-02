# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional gran
# of patent rights can be found in the PATENTS file in the same directory.

proxygen_add_test(TARGET EchoHandlerTests
  SOURCES
    EchoHandlerTest.cpp
    ../EchoServer.cpp
    ../EchoHandler.cpp
  DEPENDS
    proxygen
    proxygenhttpserver
    proxygencurl
    testmain
)
