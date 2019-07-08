add_library(proxygencurl STATIC CurlClient.cpp)
target_include_directories(
    proxygencurl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(
    proxygencurl PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)

target_link_libraries(proxygencurl PUBLIC proxygen)

add_executable(proxygen_curl CurlClientMain.cpp)
target_link_libraries(
    proxygen_curl PUBLIC
    proxygencurl
)
target_compile_options(
    proxygen_curl PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
