proxygen_add_test(TARGET StructuredHeadersTests
  SOURCES
    StructuredHeadersBufferTest.cpp
    StructuredHeadersDecoderTest.cpp
    StructuredHeadersEncoderTest.cpp
    StructuredHeadersStandardTest.cpp
    StructuredHeadersUtilitiesTest.cpp
  DEPENDS
    proxygen
    testmain
)
