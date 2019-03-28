if(NOT BUILD_TESTING)
    return()
endif()

add_library(hpacktestutils OBJECT TestUtil.cpp)
target_include_directories(
    hpacktestutils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(hpacktestutils PRIVATE proxygen)

proxygen_add_test(TARGET HPACKTests
  SOURCES
    HeaderPieceTests.cpp
    HeaderTableTests.cpp
    HPACKBufferTests.cpp
    HPACKCodecTests.cpp
    HPACKContextTests.cpp
    HPACKHeaderTests.cpp
    HuffmanTests.cpp
    LoggingTests.cpp
    QPACKCodecTests.cpp
    QPACKContextTests.cpp
    QPACKHeaderTableTests.cpp
    RFCExamplesTests.cpp
  DEPENDS
    hpacktestutils
    proxygen
    testmain
)
