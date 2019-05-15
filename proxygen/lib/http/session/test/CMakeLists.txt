proxygen_add_test(TARGET SessionTests
  SOURCES
    ByteEventTrackerTest.cpp
    DownstreamTransactionTest.cpp
    HTTPDownstreamSessionTest.cpp
    HTTPSessionAcceptorTest.cpp
    HTTPUpstreamSessionTest.cpp
    MockCodecDownstreamTest.cpp
    HTTP2PriorityQueueTest.cpp
    HTTPDefaultSessionCodecFactoryTest.cpp
    HTTPTransactionSMTest.cpp
    TestUtils.cpp
  DEPENDS
    codectestutils
    testtransport
    proxygen
    testmain
)
