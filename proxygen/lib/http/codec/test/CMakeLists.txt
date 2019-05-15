if(NOT BUILD_TESTING)
    return()
endif()

add_library(codectestutils STATIC TestUtils.cpp)
target_include_directories(
    codectestutils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(codectestutils PRIVATE proxygen)

proxygen_add_test(TARGET CodecTests
  SOURCES
    DefaultHTTPCodecFactoryTest.cpp
    FilterTests.cpp
    HTTP1xCodecTest.cpp
    HTTP2CodecTest.cpp
    HTTP2FramerTest.cpp
    SPDYCodecTest.cpp
  DEPENDS
    codectestutils
    proxygen
    testmain
)
