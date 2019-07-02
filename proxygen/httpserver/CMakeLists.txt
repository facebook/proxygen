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

add_executable(push
    samples/push/PushServer.cpp
    samples/push/PushRequestHandler.cpp
)
target_compile_options(
    push
    PRIVATE
        ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_link_libraries(
    push
    PUBLIC
        proxygen
        proxygenhttpserver
)
install(
    TARGETS push
    EXPORT proxygen-exports
    DESTINATION bin
)

add_executable(proxy
    samples/proxy/ProxyServer.cpp
    samples/proxy/ProxyHandler.cpp
)
target_compile_options(
    proxy
    PRIVATE
        ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_link_libraries(
    proxy
    PUBLIC
        proxygen
        proxygenhttpserver
)
install(
    TARGETS proxy
    EXPORT proxygen-exports
    DESTINATION bin
)

add_executable(static
    samples/static/StaticServer.cpp
    samples/static/StaticHandler.cpp
)
target_compile_options(
    static
    PRIVATE
        ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_link_libraries(
    static
    PUBLIC
        proxygen
        proxygenhttpserver
)
install(
    TARGETS static
    EXPORT proxygen-exports
    DESTINATION bin
)

add_executable(echo
    samples/echo/EchoServer.cpp
    samples/echo/EchoHandler.cpp
)
target_compile_options(
    echo
    PRIVATE
        ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_link_libraries(
    echo
    PUBLIC
        proxygen
        proxygenhttpserver
)
install(
    TARGETS echo
    EXPORT proxygen-exports
    DESTINATION bin
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
      DESTINATION bin
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
#add_subdirectory(samples/echo/test)
