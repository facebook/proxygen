# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.10)
include(CheckCXXCompilerFlag)

project(
    proxygen
)

# Set for FetchContent to skip find_package(proxygen)
set(proxygen_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  CACHE INTERNAL "proxygen source directory")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  # for in-fbsource builds
  "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
  # For shipit-transformed builds
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

option(BUILD_SHARED_LIBS
  "If enabled, build proxygen as a shared library.  \
  This is generally discouraged, since proxygen does not commit to having \
  a stable ABI."
  OFF
)
option(BUILD_SAMPLES
  "If enabled, proxygen will build various examples/samples"
  ON
)
# Mark BUILD_SHARED_LIBS as an "advanced" option, since enabling it
# is generally discouraged.
mark_as_advanced(BUILD_SHARED_LIBS)

include(FBBuildOptions)
fb_activate_static_library_option()

# PROXYGEN_FBCODE_ROOT is where the top level proxygen/ directory resides, so
# an #include <proxygen/path/to/file> will resolve to
# $PROXYGEN_FBCODE_ROOT/proxygen/path/to/file on disk
set(PROXYGEN_FBCODE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Similarly, PROXYGEN_GENERATED_ROOT is where the top level proxygen/ directory
# resides for generated files, so a #include <proxygen/path/to/generated/file>
# will be at $PROXYGEN_GENERATED_ROOT/proxygen/path/to/generated/file
set(PROXYGEN_GENERATED_ROOT ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROXYGEN_GENERATED_ROOT})

# Build-time program requirements.
if(WIN32)
    find_program(PROXYGEN_PYTHON python)
else()
    find_program(PROXYGEN_PYTHON python3)
endif()

if(NOT PROXYGEN_PYTHON)
    message(FATAL_ERROR "python is required for the proxygen build")
endif()

find_program(PROXYGEN_GPERF gperf)
if(NOT PROXYGEN_GPERF)
    message(FATAL_ERROR "gperf is required for the proxygen build")
endif()

# Dependencies
#
# IMPORTANT: If you change this, make the analogous update in:
#   cmake/proxygen-config.cmake.in
find_package(fmt REQUIRED)
# If deps are being built from source (FetchContent), skip find_package
if (NOT DEFINED folly_SOURCE_DIR)
  find_package(folly REQUIRED)
endif()
if (NOT DEFINED fizz_SOURCE_DIR)
  find_package(fizz REQUIRED)
endif()
if (NOT DEFINED wangle_SOURCE_DIR)
  find_package(wangle REQUIRED)
endif()
if (NOT DEFINED mvfst_SOURCE_DIR)
  find_package(mvfst REQUIRED)
endif()
find_package(Zstd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads)
find_package(Cares REQUIRED)

# Set install-dir defaults before any install() rule references them. The
# matching defaults below (LIB_INSTALL_DIR / CMAKE_INSTALL_DIR) stay where they
# are since nothing earlier in this file consumes them.
if (NOT DEFINED INCLUDE_INSTALL_DIR)
    set(INCLUDE_INSTALL_DIR "include")
endif()

# Logging backend selection. Default is glog (preserves historical behavior);
# downstream builds can opt in to folly::xlog or fully disable logging.
# Interpolated into lib/utils/proxygen-config.h.in as
# `#define PRX_LOGGING_@PROXYGEN_LOGGING_BACKEND@ 1` and consumed from
# proxygen/lib/utils/LogShim.h.
set(PROXYGEN_LOGGING_BACKEND "GLOG" CACHE STRING
  "Logging backend. Valid choices: GLOG, XLOG, DISABLED")
set_property(CACHE PROXYGEN_LOGGING_BACKEND PROPERTY STRINGS GLOG XLOG DISABLED)
if(NOT PROXYGEN_LOGGING_BACKEND MATCHES "^(GLOG|XLOG|DISABLED)$")
  message(FATAL_ERROR
    "PROXYGEN_LOGGING_BACKEND must be one of: GLOG, XLOG, DISABLED "
    "(got '${PROXYGEN_LOGGING_BACKEND}')")
endif()

# Glog is only required in the default GLOG backend; XLOG routes through
# folly::xlog and DISABLED strips logging entirely.
if(PROXYGEN_LOGGING_BACKEND STREQUAL "GLOG")
  find_package(Glog REQUIRED)

  # Propagate glog's required compile definition to all proxygen targets.
  # glog 0.7+ requires GLOG_USE_GLOG_EXPORT but since glog::glog is linked
  # PRIVATE on the monolithic library, OBJECT libraries don't inherit it.
  get_target_property(_glog_defs glog::glog INTERFACE_COMPILE_DEFINITIONS)
  if(_glog_defs)
    add_compile_definitions(${_glog_defs})
  endif()
endif()

# Generate <proxygen/proxygen-config.h> for this build. The template
# interpolates PROXYGEN_LOGGING_BACKEND into a
# `#define PRX_LOGGING_<BACKEND> 1`, which proxygen/lib/utils/LogShim.h
# consumes to pick the macro implementations.
# Generated into PROXYGEN_GENERATED_ROOT which is already on every proxygen
# target's include path (see ProxygenFunctions.cmake).
configure_file(
  proxygen/lib/utils/proxygen-config.h.in
  ${PROXYGEN_GENERATED_ROOT}/proxygen/proxygen-config.h)
install(FILES ${PROXYGEN_GENERATED_ROOT}/proxygen/proxygen-config.h
  DESTINATION ${INCLUDE_INSTALL_DIR}/proxygen/)

list(APPEND
    _PROXYGEN_COMMON_COMPILE_OPTIONS
    -Wall
    -Wextra
)

CHECK_CXX_COMPILER_FLAG(-Wnoexcept-type COMPILER_HAS_W_NOEXCEPT_TYPE)
if (COMPILER_HAS_W_NOEXCEPT_TYPE)
  list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-noexcept-type)
endif()
CHECK_CXX_COMPILER_FLAG(-Wunused-parameter COMPILER_HAS_W_UNUSED_PARAMETER)
if (COMPILER_HAS_W_UNUSED_PARAMETER)
  list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-unused-parameter)
endif()
CHECK_CXX_COMPILER_FLAG(-Wmissing-field-initializers COMPILER_HAS_W_MISSING_FIELD_INITIALIZERS)
if (COMPILER_HAS_W_MISSING_FIELD_INITIALIZERS)
  list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-missing-field-initializers)
endif()
CHECK_CXX_COMPILER_FLAG(-Wnullability-completeness COMPILER_HAS_W_NULLABILITY_COMPLETENESS)
if (COMPILER_HAS_W_NULLABILITY_COMPLETENESS)
    list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-nullability-completeness)
endif()
CHECK_CXX_COMPILER_FLAG(-Wdeprecated-register COMPILER_HAS_W_DEPRECATED_REGISTER)
if (COMPILER_HAS_W_DEPRECATED_REGISTER)
    list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-deprecated-register)
endif()
CHECK_CXX_COMPILER_FLAG(-Wregister COMPILER_HAS_W_REGISTER)
if (COMPILER_HAS_W_REGISTER)
    list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-register)
endif()
CHECK_CXX_COMPILER_FLAG(-Wunused-value COMPILER_HAS_W_UNUSED_VALUE)
if (COMPILER_HAS_W_UNUSED_VALUE)
    list(APPEND _PROXYGEN_COMMON_COMPILE_OPTIONS -Wno-unused-value)
endif()

SET(GFLAG_DEPENDENCIES "")
SET(PROXYGEN_EXTRA_LINK_LIBRARIES "")
SET(PROXYGEN_EXTRA_INCLUDE_DIRECTORIES "")

find_package(gflags CONFIG QUIET)
if (gflags_FOUND)
  message("module path: ${CMAKE_MODULE_PATH}")
  message(STATUS "Found gflags from package config")
  if (TARGET gflags-shared)
    list(APPEND GFLAG_DEPENDENCIES gflags-shared)
  elseif (TARGET gflags)
    list(APPEND GFLAG_DEPENDENCIES gflags)
  else()
    message(FATAL_ERROR
            "Unable to determine the target name for the GFlags package.")
  endif()
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${GFLAGS_LIBRARIES})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${GFLAGS_INCLUDE_DIR})
else()
  find_package(Gflags REQUIRED MODULE)
  list(APPEND PROXYGEN_EXTRA_LINK_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND PROXYGEN_EXTRA_INCLUDE_DIRECTORIES ${LIBGFLAGS_INCLUDE_DIR})
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBGFLAGS_LIBRARY})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBGFLAGS_INCLUDE_DIR})
endif()

include(ProxygenTest)
include(ProxygenFunctions)

# Set PROXYGEN_DIR for proxygen_add_library to compute target names
set(PROXYGEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proxygen)

add_subdirectory(proxygen)

# The generated lib/utils/CMakeLists.txt lists both Folly::folly_glog and
# Folly::folly_logging_logging as unconditional INTERFACE deps for the log
# shim. Narrow that to match the selected backend so downstream code doesn't
# pull in libraries it isn't actually using.
if(PROXYGEN_LOGGING_BACKEND STREQUAL "XLOG")
  set_target_properties(proxygen_utils_log_shim PROPERTIES
    INTERFACE_LINK_LIBRARIES
    "proxygen_utils_config;Folly::folly_logging_logging")
elseif(PROXYGEN_LOGGING_BACKEND STREQUAL "DISABLED")
  set_target_properties(proxygen_utils_log_shim PROPERTIES
    INTERFACE_LINK_LIBRARIES "proxygen_utils_config")
endif()

# Resolve deferred dependencies for granular libraries
# (monolithic proxygen target is manually maintained in proxygen/lib/CMakeLists.txt)
proxygen_resolve_deferred_dependencies()

if (NOT DEFINED LIB_INSTALL_DIR)
    set(LIB_INSTALL_DIR "lib")
endif()

if (NOT DEFINED INCLUDE_INSTALL_DIR)
    set(INCLUDE_INSTALL_DIR "include")
endif()

if (NOT DEFINED CMAKE_INSTALL_DIR)
    set(CMAKE_INSTALL_DIR "${LIB_INSTALL_DIR}/cmake/proxygen/")
endif()

install(
    EXPORT proxygen-exports
    FILE proxygen-targets.cmake
    NAMESPACE proxygen::
    DESTINATION ${CMAKE_INSTALL_DIR}
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/proxygen-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    DESTINATION ${CMAKE_INSTALL_DIR}
)

# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
