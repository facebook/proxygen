#  Copyright (c) 2019, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

option(BUILD_TESTING  "Enable tests" OFF)
include(CTest)
if(BUILD_TESTING)
    include(GoogleTest)
    find_package(GMock MODULE REQUIRED)
endif()

function(proxygen_add_test)
    if(NOT BUILD_TESTING)
        return()
    endif()

    set(options)
    set(one_value_args TARGET WORKING_DIRECTORY PREFIX)
    set(multi_value_args SOURCES DEPENDS INCLUDES EXTRA_ARGS)
    cmake_parse_arguments(PARSE_ARGV 0 PROXYGEN_TEST "${options}" "${one_value_args}" "${multi_value_args}")

    if(NOT PROXYGEN_TEST_TARGET)
        message(FATAL_ERROR "The TARGET parameter is mandatory.")
    endif()

    if(NOT PROXYGEN_TEST_SOURCES)
        set(PROXYGEN_TEST_SOURCES "${PROXYGEN_TEST_TARGET}.cpp")
    endif()

    add_executable(${PROXYGEN_TEST_TARGET} "${PROXYGEN_TEST_SOURCES}")
    target_link_libraries(${PROXYGEN_TEST_TARGET} PRIVATE "${PROXYGEN_TEST_DEPENDS}")
    target_include_directories(${PROXYGEN_TEST_TARGET} PRIVATE "${PROXYGEN_TEST_INCLUDES}")

    gtest_add_tests(TARGET ${PROXYGEN_TEST_TARGET}
                    EXTRA_ARGS "${PROXYGEN_TEST_EXTRA_ARGS}"
                    WORKING_DIRECTORY ${PROXYGEN_TEST_WORKING_DIRECTORY}
                    TEST_PREFIX ${PROXYGEN_TEST_PREFIX}
                    TEST_LIST PROXYGEN_TEST_CASES)

    target_link_libraries(${PROXYGEN_TEST_TARGET} PRIVATE ${LIBGMOCK_LIBRARIES})
    target_include_directories(${PROXYGEN_TEST_TARGET} SYSTEM PRIVATE ${LIBGMOCK_INCLUDE_DIR})
    target_compile_definitions(${PROXYGEN_TEST_TARGET} PRIVATE ${LIBGMOCK_DEFINES})
    set_tests_properties(${PROXYGEN_TEST_CASES} PROPERTIES TIMEOUT 120)
endfunction()
