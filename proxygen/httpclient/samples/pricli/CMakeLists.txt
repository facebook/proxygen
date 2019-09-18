add_executable(proxygen_pricli PriCli.cpp)
target_compile_options(
    proxygen_pricli PRIVATE
    ${_PROXYGEN_COMMON_COMPILE_OPTIONS}
)
