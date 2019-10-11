# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

add_library(
    proxygen_http_parser OBJECT
    http_parser/http_parser_cpp.cpp
)

target_compile_options(
    proxygen_http_parser PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_include_directories(
    proxygen_http_parser PRIVATE
    ${PROXYGEN_FBCODE_ROOT}
)
install(FILES http_parser/http_parser.h DESTINATION
        include/proxygen/external/http_parser)
