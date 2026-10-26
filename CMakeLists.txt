cmake_minimum_required(VERSION 3.16)

project(falafel_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(FALAFEL_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host tests"
    OFF
)

enable_testing()
find_package(Threads REQUIRED)
include(FetchContent)

set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable Mbed TLS programs" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Disable Mbed TLS tests" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(mbedtls)

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

add_library(falafel_crypto STATIC shared/crypto/falafel_crypto_mbedtls.c)
target_include_directories(falafel_crypto PUBLIC shared/include)
target_link_libraries(falafel_crypto PUBLIC MbedTLS::mbedcrypto)

add_executable(test_protocol
    test/test_protocol.c
)

target_link_libraries(test_protocol PRIVATE
    falafel_protocol
)

add_executable(test_app
    test/test_app.c
)
add_executable(test_crypto test/test_crypto.c)
target_link_libraries(test_crypto PRIVATE falafel_crypto)
if(UNIX)
    add_executable(test_desktop_state test/test_desktop_state.c simulator/src/desktop_transport.c)
    target_include_directories(test_desktop_state PRIVATE shared/include simulator/src)
    target_link_libraries(test_desktop_state PRIVATE falafel_app Threads::Threads)
    add_executable(test_transport_e2e test/test_transport_e2e.c simulator/src/desktop_transport.c)
    target_include_directories(test_transport_e2e PRIVATE shared/include simulator/src)
    target_link_libraries(test_transport_e2e PRIVATE falafel_app Threads::Threads)
endif()

target_link_libraries(test_app PRIVATE
    falafel_app
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_protocol PRIVATE -Wall -Wextra -Werror)
    target_compile_options(falafel_app PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_protocol PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_options(test_app PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_compile_options(falafel_crypto PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_crypto PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    if(TARGET test_desktop_state)
        target_compile_options(test_desktop_state PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endif()
    if(TARGET test_transport_e2e)
        target_compile_options(test_transport_e2e PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endif()
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
    target_compile_options(falafel_crypto PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(test_crypto PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_protocol PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_app PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_crypto PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    if(TARGET test_desktop_state)
        target_compile_options(test_desktop_state PRIVATE ${FALAFEL_SANITIZER_FLAGS})
        target_link_options(test_desktop_state PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    endif()
    if(TARGET test_transport_e2e)
        target_compile_options(test_transport_e2e PRIVATE ${FALAFEL_SANITIZER_FLAGS})
        target_link_options(test_transport_e2e PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    endif()
endif()

add_test(NAME protocol_init COMMAND test_protocol init)
add_test(NAME protocol_serialization COMMAND test_protocol serialization)
add_test(NAME protocol_deserialization COMMAND test_protocol deserialization)
add_test(NAME protocol_invalid_packets COMMAND test_protocol invalid-packets)
add_test(NAME protocol_payload_boundaries COMMAND test_protocol payload-boundaries)
add_test(NAME protocol_events COMMAND test_protocol)
add_test(NAME app_init COMMAND test_app)
add_test(NAME crypto_primitives COMMAND test_crypto)
if(TARGET test_desktop_state)
    add_test(NAME desktop_persistence COMMAND test_desktop_state)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME relay_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_relay.py)
set_tests_properties(relay_integration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 15)
if(TARGET test_transport_e2e)
    add_test(NAME transport_end_to_end COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/test/test_transport_e2e.py $<TARGET_FILE:test_transport_e2e>)
    set_tests_properties(transport_end_to_end PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 20)
endif()
