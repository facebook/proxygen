# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# Add lhttp library
add_library(
    proxygen_lhttp OBJECT
    lhttp/api.c
    lhttp/http.c
    lhttp/llhttp.c
)

target_compile_options(
    proxygen_lhttp PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

if (BUILD_SHARED_LIBS)
  set_property(TARGET proxygen_lhttp PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(
    proxygen_lhttp PRIVATE
    ${PROXYGEN_FBCODE_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/lhttp
)

# Add http_parser URL parsing functionality
add_library(
    proxygen_http_parser OBJECT
    http_parser/http_parser_url.cpp
)

target_compile_options(
    proxygen_http_parser PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

if (BUILD_SHARED_LIBS)
  set_property(TARGET proxygen_http_parser PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(
    proxygen_http_parser PRIVATE
    ${PROXYGEN_FBCODE_ROOT}
)

# Install headers
install(FILES http_parser/http_parser.h DESTINATION
        include/proxygen/external/http_parser)
install(FILES lhttp/api.h DESTINATION
        include/proxygen/external/lhttp)
