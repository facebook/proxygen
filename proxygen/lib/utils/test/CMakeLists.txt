proxygen_add_test(TARGET AsyncTimeoutSetTest DEPENDS proxygen testmain)
proxygen_add_test(TARGET TraceEventTest DEPENDS proxygen testmain)

proxygen_add_test(TARGET UtilTests
  SOURCES
    Base64Test.cpp
    CryptUtilTest.cpp
    GenericFilterTest.cpp
    HTTPTimeTest.cpp
    LoggingTests.cpp
    ParseURLTest.cpp
    PerfectIndexMapTest.cpp
    RendezvousHashTest.cpp
    TimeTest.cpp
    UtilTest.cpp
    ZlibTests.cpp
    ZstdTests.cpp
  DEPENDS
    proxygen
    testmain
)
