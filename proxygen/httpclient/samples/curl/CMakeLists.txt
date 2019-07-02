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

add_executable(pcurl CurlClientMain.cpp)
target_link_libraries(
    pcurl PUBLIC
    proxygencurl
)
target_compile_options(
    pcurl PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
