# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

add_library(proxygencurl STATIC CurlClient.cpp)
target_include_directories(
    proxygencurl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(
    proxygencurl PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

target_link_libraries(proxygencurl PUBLIC proxygen)

add_executable(proxygen_curl CurlClientMain.cpp)
target_link_libraries(
    proxygen_curl PUBLIC
    proxygencurl
)
target_compile_options(
    proxygen_curl PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

install(
    TARGETS proxygen_curl
    EXPORT proxygen-exports
    DESTINATION bin
)
