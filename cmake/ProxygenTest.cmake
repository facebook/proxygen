# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

option(BUILD_TESTS  "Enable tests" OFF)
include(CTest)
if(BUILD_TESTS)
  include(GoogleTest)
  include(ExternalProject)

	# Download and install GoogleMock
	ExternalProject_Add(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG release-1.8.0
		PREFIX googletest
		# Disable install step
		INSTALL_COMMAND ""
		CMAKE_CACHE_ARGS
			-DCMAKE_BUILD_TYPE:STRING=Release
			-DBUILD_GMOCK:BOOL=ON
			-DBUILD_GTEST:BOOL=ON
			-Dgtest_force_shared_crt:BOOL=OFF
	)
  ExternalProject_Get_Property(googletest source_dir)
  set(GTEST_SOURCE_DIR ${source_dir})
  ExternalProject_Get_Property(googletest binary_dir)
  set(GTEST_BINARY_DIR ${binary_dir})

  # Setup gtest / gmock libraries and include dirs
  set(LIBGTEST_LIBRARIES
    "${GTEST_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/gtest/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${GTEST_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/gtest/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(LIBGMOCK_LIBRARIES
    "${GTEST_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/${CMAKE_STATIC_LIBRARY_PREFIX}gmock${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${GTEST_BINARY_DIR}/${CMAKE_CFG_INTDIR}/googlemock/${CMAKE_STATIC_LIBRARY_PREFIX}gmock_main${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
  set(LIBGMOCK_INCLUDE_DIR "${source_dir}/googlemock/include")
  set(LIBGTEST_INCLUDE_DIR "${source_dir}/googletest/include")
endif()

function(proxygen_add_test)
    if(NOT BUILD_TESTS)
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

    add_executable(${PROXYGEN_TEST_TARGET} 
      "${PROXYGEN_TEST_SOURCES}"
    )
    add_dependencies(${PROXYGEN_TEST_TARGET} googletest)
    target_link_libraries(${PROXYGEN_TEST_TARGET} PRIVATE 
      "${PROXYGEN_TEST_DEPENDS}"
    )
    target_include_directories(${PROXYGEN_TEST_TARGET} PRIVATE 
      "${PROXYGEN_TEST_INCLUDES}"
    )
    target_compile_options(${PROXYGEN_TEST_TARGET} PRIVATE 
      "${_PROXYGEN_COMMON_COMPILE_OPTIONS}"
    )

    gtest_add_tests(TARGET ${PROXYGEN_TEST_TARGET}
                    EXTRA_ARGS "${PROXYGEN_TEST_EXTRA_ARGS}"
                    WORKING_DIRECTORY ${PROXYGEN_TEST_WORKING_DIRECTORY}
                    TEST_PREFIX ${PROXYGEN_TEST_PREFIX}
                    TEST_LIST PROXYGEN_TEST_CASES)

    target_link_libraries(${PROXYGEN_TEST_TARGET} PRIVATE 
      ${LIBGMOCK_LIBRARIES}
    )
    target_include_directories(${PROXYGEN_TEST_TARGET} SYSTEM PRIVATE 
      ${LIBGMOCK_INCLUDE_DIR}
      ${LIBGTEST_INCLUDE_DIR}
    )
    target_compile_definitions(${PROXYGEN_TEST_TARGET} PRIVATE ${LIBGMOCK_DEFINES})
    set_tests_properties(${PROXYGEN_TEST_CASES} PROPERTIES TIMEOUT 120)
endfunction()
