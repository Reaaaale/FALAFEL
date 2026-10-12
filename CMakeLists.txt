cmake_minimum_required(VERSION 3.16)

project(falafel_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_library(falafel_protocol STATIC
    shared/app/falafel_protocol.c
)

target_include_directories(falafel_protocol PUBLIC
    shared/include
)

add_executable(test_protocol
    test/test_protocol.c
)

target_link_libraries(test_protocol PRIVATE
    falafel_protocol
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_protocol PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_protocol PRIVATE -Wall -Wextra -Werror)
endif()

add_test(
    NAME protocol_test
    COMMAND test_protocol
)