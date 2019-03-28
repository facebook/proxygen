add_library(proxygencurl OBJECT CurlClient.cpp)
target_include_directories(
    proxygencurl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(proxygencurl PUBLIC proxygen)

add_executable(curl CurlClientMain.cpp)
target_link_libraries(curl PRIVATE proxygencurl)
