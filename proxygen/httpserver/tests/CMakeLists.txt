proxygen_add_test(TARGET HTTPServerTests
  SOURCES
    HTTPServerTest.cpp
    RequestHandlerAdaptorTest.cpp
  DEPENDS
    proxygen
    proxygenhttpserver
    proxygencurl
    testmain
)
