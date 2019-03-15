add_library(
    proxygen_http_parser OBJECT
    http_parser/http_parser_cpp.cpp
)

target_compile_options(
    proxygen_http_parser PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
target_include_directories(
    proxygen_http_parser PRIVATE
    ${PROXYGEN_FBCODE_ROOT}
)
