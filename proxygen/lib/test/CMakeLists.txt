if(NOT BUILD_TESTING)
    return()
endif()

add_library(testtransport STATIC TestAsyncTransport.cpp)
target_include_directories(
    testtransport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(testtransport PRIVATE proxygen)

add_library(testmain STATIC TestMain.cpp)
target_link_libraries(testmain PRIVATE Folly::folly)
