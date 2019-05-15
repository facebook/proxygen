proxygen_add_test(TARGET LibHTTPTests
  SOURCES
    HTTPCommonHeadersTests.cpp
    HTTPMessageTest.cpp
    RFC2616Test.cpp
    WindowTest.cpp
  DEPENDS
    proxygen
    testmain
)
