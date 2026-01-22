# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

add_library(httperf2 HTTPerf2.cpp Client.cpp)
target_include_directories(
    httperf2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(
    httperf2 PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

if (BUILD_SHARED_LIBS)
    set_property(TARGET httperf2 PROPERTY POSITION_INDEPENDENT_CODE ON)
    if (DEFINED PACKAGE_VERSION)
        set_target_properties(
            httperf2 PROPERTIES VERSION ${PACKAGE_VERSION}
        )
    endif()
endif()

target_link_libraries(
    httperf2 PUBLIC
    proxygenhqloggerhelper
    proxygen
)

install(
    TARGETS httperf2
    EXPORT proxygen-exports
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
)

add_executable(proxygen_httperf2 Main.cpp)
target_link_libraries(
    proxygen_httperf2 PUBLIC
    httperf2
)
target_compile_options(
    proxygen_httperf2 PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

install(
    TARGETS proxygen_httperf2
    EXPORT proxygen-exports
    DESTINATION bin
)
