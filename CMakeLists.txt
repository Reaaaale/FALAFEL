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

add_library(falafel_async_e2ee STATIC shared/e2ee/falafel_async_e2ee.c)
target_link_libraries(falafel_async_e2ee PUBLIC falafel_crypto)
add_executable(test_async_e2ee test/test_async_e2ee.c)
target_link_libraries(test_async_e2ee PRIVATE falafel_async_e2ee)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_async_e2ee PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_async_e2ee PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
if(FALAFEL_ENABLE_SANITIZERS)
    target_compile_options(falafel_async_e2ee PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(test_async_e2ee PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_async_e2ee PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()
add_test(NAME async_e2ee COMMAND test_async_e2ee)

add_executable(test_async_faults test/test_async_faults.c)
target_link_libraries(test_async_faults PRIVATE falafel_crypto)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_async_faults PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
if(FALAFEL_ENABLE_SANITIZERS)
    target_compile_options(test_async_faults PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_async_faults PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()
add_test(NAME async_provider_faults COMMAND test_async_faults)
option(FALAFEL_ASYNC_NOISE_INTEROP "Test offline envelopes against pinned Noise-C" OFF)
if(FALAFEL_ASYNC_NOISE_INTEROP)
    include(cmake/noise_c_pinned.cmake)
    include(experiments/noise-feasibility/noise_sources.cmake)
    add_library(async_noise_reference STATIC ${NOISE_PROTOCOL_SOURCES})
    target_include_directories(async_noise_reference PUBLIC "${NOISE_C_SOURCE_DIR}/include")
    target_include_directories(async_noise_reference PRIVATE "${NOISE_C_SOURCE_DIR}/src" "${NOISE_C_SOURCE_DIR}/src/protocol")
    target_compile_definitions(async_noise_reference PRIVATE USE_OPENSSL=0 USE_LIBSODIUM=0)
    add_executable(test_async_interop test/test_async_interop.c)
    target_link_libraries(test_async_interop PRIVATE falafel_async_e2ee async_noise_reference)
    target_compile_options(test_async_interop PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    if(FALAFEL_ENABLE_SANITIZERS)
        foreach(t async_noise_reference test_async_interop)
            target_compile_options(${t} PRIVATE ${FALAFEL_SANITIZER_FLAGS})
        endforeach()
        target_link_options(test_async_interop PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    endif()
    add_test(NAME async_noise_interop COMMAND test_async_interop)
endif()

add_library(falafel_mailbox STATIC shared/e2ee/falafel_mailbox.c)
target_link_libraries(falafel_mailbox PUBLIC falafel_async_e2ee)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_mailbox PRIVATE -Wall -Wextra -Werror)
endif()
if(FALAFEL_ENABLE_SANITIZERS)
    target_compile_options(falafel_mailbox PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()
if(UNIX)
    add_library(falafel_mailbox_posix STATIC shared/storage/falafel_mailbox_posix.c)
    target_link_libraries(falafel_mailbox_posix PUBLIC falafel_mailbox)
    add_executable(test_mailbox test/test_mailbox.c)
    target_link_libraries(test_mailbox PRIVATE falafel_mailbox_posix)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(falafel_mailbox_posix PRIVATE -Wall -Wextra -Werror)
        target_compile_options(test_mailbox PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    endif()
    if(FALAFEL_ENABLE_SANITIZERS)
        foreach(t falafel_mailbox_posix test_mailbox)
            target_compile_options(${t} PRIVATE ${FALAFEL_SANITIZER_FLAGS})
        endforeach()
        target_link_options(test_mailbox PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    endif()
    add_test(NAME mailbox_persistence COMMAND test_mailbox)
endif()

add_library(falafel_mail_link STATIC shared/e2ee/falafel_mail_link.c)
target_link_libraries(falafel_mail_link PUBLIC falafel_mailbox)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_mail_link PRIVATE -Wall -Wextra -Werror)
endif()
if(FALAFEL_ENABLE_SANITIZERS)
    target_compile_options(falafel_mail_link PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()
if(UNIX)
    add_executable(mailbox_peer experiments/desktop-mailbox/mailbox_peer.c simulator/src/desktop_transport.c)
    target_include_directories(mailbox_peer PRIVATE simulator/src)
    target_link_libraries(mailbox_peer PRIVATE falafel_mail_link falafel_mailbox_posix falafel_app Threads::Threads)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(mailbox_peer PRIVATE -Wall -Wextra -Werror)
    endif()
    if(FALAFEL_ENABLE_SANITIZERS)
        target_compile_options(mailbox_peer PRIVATE ${FALAFEL_SANITIZER_FLAGS})
        target_link_options(mailbox_peer PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    endif()
    add_test(NAME encrypted_tcp_end_to_end COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/test/test_mail_link.py $<TARGET_FILE:mailbox_peer>)
    set_tests_properties(encrypted_tcp_end_to_end PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 90)
endif()

# Compile the actual ESP-IDF adapter on the host against a fault-injectable API
# model. The model tests adapter/recovery logic, NOT physical NVS atomicity.
add_library(falafel_history STATIC shared/storage/falafel_history.c)
target_link_libraries(falafel_history PUBLIC falafel_mailbox)
add_executable(test_storage_espidf
    test/test_storage_espidf.c test/storage_nvs_model/nvs_model.c
    firmware/components/falafel_storage/falafel_storage_espidf.c)
target_include_directories(test_storage_espidf PRIVATE
    test/storage_nvs_model firmware/components/falafel_storage/include)
target_link_libraries(test_storage_espidf PRIVATE falafel_history)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(falafel_history PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_storage_espidf PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
if(FALAFEL_ENABLE_SANITIZERS)
    target_compile_options(falafel_history PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_compile_options(test_storage_espidf PRIVATE ${FALAFEL_SANITIZER_FLAGS})
    target_link_options(test_storage_espidf PRIVATE ${FALAFEL_SANITIZER_FLAGS})
endif()
add_test(NAME embedded_storage_model COMMAND test_storage_espidf)
