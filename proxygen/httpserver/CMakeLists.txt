add_library(
    proxygenhttpserver STATIC
    RequestHandlerAdaptor.cpp
    SignalHandler.cpp
    HTTPServerAcceptor.cpp
    HTTPServer.cpp
)
target_compile_options(
    proxygenhttpserver
    PRIVATE
        ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

target_link_libraries(
    proxygenhttpserver
    PUBLIC
        proxygen
)

install(
    TARGETS proxygenhttpserver
    EXPORT proxygen-exports
    DESTINATION lib
)


if (BUILD_QUIC)
  add_executable(hq
      samples/hq/main.cpp
      samples/hq/SampleHandlers.cpp
  )

  target_compile_options(
      hq
      PRIVATE
          ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
  )

  target_link_libraries(
      hq
      PUBLIC
          proxygen
          proxygencurl
          proxygenhttpserver
          mvfst::mvfst_transport
          mvfst::mvfst_client
          mvfst::mvfst_server
  )

  install(
      TARGETS hq
      EXPORT proxygen-exports
      DESTINATION lib
  )
endif()

file(
    GLOB_RECURSE PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.h
)
list(FILTER PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL EXCLUDE REGEX tests/)
list(FILTER PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL EXCLUDE REGEX Mocks.h)
list(FILTER PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL EXCLUDE REGEX samples/)
foreach(header ${PROXYGEN_HTTPSERVER_HEADERS_TOINSTALL})
    get_filename_component(header_dir ${header} DIRECTORY)
    install(FILES ${header} DESTINATION include/proxygen/httpserver/${header_dir})
endforeach()

add_subdirectory(tests)
add_subdirectory(filters/tests)
