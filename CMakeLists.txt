cmake_minimum_required(VERSION 3.10)

project(
    proxygen
)

set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  # for in-fbsource builds
  "${CMAKE_CURRENT_SOURCE_DIR}/../opensource/fbcode_builder/CMake"
  # For shipit-transformed builds
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

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
find_program(PROXYGEN_PYTHON python)
if(PROXYGEN_PYTHON-NOTFOUND)
    message(FATAL_ERROR "python is required for the proxygen build")
endif()

find_program(PROXYGEN_GPERF gperf)
if(PROXYGEN_GPERF-NOTFOUND)
    message(FATAL_ERROR "gperf is required for the proxygen build")
endif()

# Dependencies
find_package(folly REQUIRED)
find_package(Fizz REQUIRED)
find_package(wangle REQUIRED)
find_package(Zstd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads)
find_package(Boost 1.58 REQUIRED
  COMPONENTS
    iostreams
    context
    chrono
    date_time
    filesystem
    program_options
    regex
    system
    thread
)

list(APPEND
    _PROXYGEN_COMMON_COMPILE_OPTIONS
    -std=c++14
    -Wall
    -Wextra
)

include(ProxygenTest)

add_subdirectory(proxygen)

install(
    EXPORT proxygen-exports
    FILE proxygen-targets.cmake
    NAMESPACE proxygen::
    DESTINATION lib/cmake/proxygen/
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/proxygen-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    INSTALL_DESTINATION lib/cmake/proxygen/
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/proxygen-config.cmake
    DESTINATION lib/cmake/proxygen/
)
