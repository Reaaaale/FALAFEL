cmake_minimum_required(VERSION 3.16)

project(falafel_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(FALAFEL_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host tests"
    OFF
)

enable_testing()

add_library(falafel_protocol STATIC
    shared/app/falafel_protocol.c
)

target_include_directories(falafel_protocol PUBLIC
    shared/include
)

add_library(falafel_app STATIC
    shared/app/falafel_app.c
)

target_include_directories(falafel_app PUBLIC
    shared/include
)

target_link_libraries(falafel_app PUBLIC
    falafel_protocol
)

add_executable(test_protocol
    test/test_protocol.c
)

target_link_libraries(test_protocol PRIVATE
    falafel_protocol
)

add_executable(test_app
    test/test_app.c
)

target_link_libraries(test_app PRIVATE
    falafel_app
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_protocol PRIVATE -Wall -Wextra -Werror)
    target_compile_options(falafel_app PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_protocol PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_options(test_app PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()

if(FALAFEL_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "FALAFEL_ENABLE_SANITIZERS requires a GCC- or Clang-compatible compiler"
        )
    endif()

    include(CheckCSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS
        "-fsanitize=address,undefined -fno-omit-frame-pointer"
    )
    set(CMAKE_REQUIRED_LINK_OPTIONS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    check_c_source_compiles(
        "int main(void) { return 0; }"
        FALAFEL_SANITIZER_FLAGS_SUPPORTED
    )
    cmake_pop_check_state()

    if(NOT FALAFEL_SANITIZER_FLAGS_SUPPORTED)
        message(FATAL_ERROR
            "FALAFEL_ENABLE_SANITIZERS is ON, but the selected compiler does not support the required sanitizer flags"
        )
    endif()

    set(FALAFEL_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )

    target_compile_options(falafel_protocol PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(falafel_app PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(test_protocol PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(test_app PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_protocol PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_app PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()

add_test(NAME protocol_init COMMAND test_protocol init)
add_test(NAME protocol_serialization COMMAND test_protocol serialization)
add_test(NAME protocol_deserialization COMMAND test_protocol deserialization)
add_test(NAME protocol_invalid_packets COMMAND test_protocol invalid-packets)
add_test(NAME protocol_payload_boundaries COMMAND test_protocol payload-boundaries)
add_test(NAME app_init COMMAND test_app)
