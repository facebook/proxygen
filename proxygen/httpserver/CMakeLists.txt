# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# Manually maintained (generate_cmake.py skips this directory)

proxygen_add_library(proxygenhttpserver
  EXCLUDE_FROM_MONOLITH
  SRCS
    RequestHandlerAdaptor.cpp
    SignalHandler.cpp
    HTTPServerAcceptor.cpp
    HTTPServer.cpp
  EXPORTED_DEPS
    proxygen
)

# Include granular HQ server targets (unconditional - needed by moxygen)
add_subdirectory(samples/hq)

# Backwards compatibility alias for proxygenhqserver
# Links to granular targets instead of monolithic proxygen
add_library(proxygenhqserver INTERFACE)
target_link_libraries(proxygenhqserver INTERFACE
  proxygen_httpserver_samples_hq_HQServer
  proxygen_httpserver_samples_hq_fizz_context
  proxygen_httpserver_samples_hq_hq_logger_helper
)
install(TARGETS proxygenhqserver EXPORT proxygen-exports)
add_library(proxygen::proxygenhqserver ALIAS proxygenhqserver)

# Backwards compatibility alias for proxygenhqloggerhelper
add_library(proxygenhqloggerhelper INTERFACE)
target_link_libraries(proxygenhqloggerhelper INTERFACE
  proxygen_httpserver_samples_hq_hq_logger_helper
)
install(TARGETS proxygenhqloggerhelper EXPORT proxygen-exports)
add_library(proxygen::proxygenhqloggerhelper ALIAS proxygenhqloggerhelper)

if (BUILD_SAMPLES)
  proxygen_add_library(proxygendeviousbaton
    EXCLUDE_FROM_MONOLITH
    SRCS
      samples/hq/devious/DeviousBaton.cpp
    EXPORTED_DEPS
      proxygen
  )

  add_executable(proxygen_push
    samples/push/PushServer.cpp
    samples/push/PushRequestHandler.cpp
  )
  target_compile_options(
      proxygen_push PRIVATE ${_PROXYGEN_COMMON_COMPILE_OPTIONS})
  target_link_libraries(proxygen_push PUBLIC proxygen proxygenhttpserver)
  install(TARGETS proxygen_push EXPORT proxygen-exports DESTINATION bin)

  add_executable(proxygen_proxy
    samples/proxy/ProxyServer.cpp
    samples/proxy/ProxyHandler.cpp
  )
  target_compile_options(
      proxygen_proxy PRIVATE ${_PROXYGEN_COMMON_COMPILE_OPTIONS})
  target_link_libraries(proxygen_proxy PUBLIC proxygen proxygenhttpserver)
  install(TARGETS proxygen_proxy EXPORT proxygen-exports DESTINATION bin)

  add_executable(proxygen_static
    samples/static/StaticServer.cpp
    samples/static/StaticHandler.cpp
  )
  target_compile_options(
      proxygen_static PRIVATE ${_PROXYGEN_COMMON_COMPILE_OPTIONS})
  target_link_libraries(proxygen_static PUBLIC proxygen proxygenhttpserver)
  install(TARGETS proxygen_static EXPORT proxygen-exports DESTINATION bin)

  add_executable(proxygen_echo
    samples/echo/EchoServer.cpp
    samples/echo/EchoHandler.cpp
  )
  target_compile_options(
      proxygen_echo PRIVATE ${_PROXYGEN_COMMON_COMPILE_OPTIONS})
  target_link_libraries(proxygen_echo PUBLIC proxygen proxygenhttpserver)
  install(TARGETS proxygen_echo EXPORT proxygen-exports DESTINATION bin)

  add_executable(hq
    samples/hq/main.cpp
    samples/hq/FizzContext.cpp
    samples/hq/H2Server.cpp
    samples/hq/HQClient.cpp
    samples/hq/HQCommandLine.cpp
    samples/hq/HQServer.cpp
    samples/hq/HQServerModule.cpp
    samples/hq/HQParams.cpp
    samples/hq/SampleHandlers.cpp
  )
  target_compile_options(hq PRIVATE ${_PROXYGEN_COMMON_COMPILE_OPTIONS})
  target_link_libraries(hq
    PUBLIC
      proxygen
      proxygencurl
      proxygenhttpserver
      proxygen_httpserver_samples_hq_hq_logger_helper
      proxygendeviousbaton
      mvfst::mvfst_transport
      mvfst::mvfst_client
      mvfst::mvfst_server
  )
  install(TARGETS hq EXPORT proxygen-exports DESTINATION bin)
endif()

file(
  GLOB_RECURSE PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.h
)
list(FILTER PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL EXCLUDE REGEX tests/)
foreach(header ${PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL})
  get_filename_component(header_dir ${header} DIRECTORY)
  install(FILES ${header} DESTINATION include/proxygen/httpserver/${header_dir})
endforeach()

add_subdirectory(tests)
add_subdirectory(filters/tests)
#add_subdirectory(samples/echo/test)
#add_subdirectory(samples/hq/devious/test)
