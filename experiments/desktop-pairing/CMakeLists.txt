cmake_minimum_required(VERSION 3.16)
project(falafel_desktop_pairing C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(FALAFEL_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

include(../../cmake/noise_c_pinned.cmake)

include(../noise-feasibility/noise_sources.cmake)
add_library(noise_protocol STATIC ${NOISE_PROTOCOL_SOURCES})
target_include_directories(noise_protocol PUBLIC "${NOISE_C_SOURCE_DIR}/include")
target_include_directories(noise_protocol PRIVATE "${NOISE_C_SOURCE_DIR}/src" "${NOISE_C_SOURCE_DIR}/src/protocol")
target_compile_definitions(noise_protocol PRIVATE USE_OPENSSL=0 USE_LIBSODIUM=0)

add_library(falafel_pairing STATIC
    ../../shared/pairing/falafel_pairing.c
    ../../shared/pairing/falafel_pairing_persistence.c)
target_include_directories(falafel_pairing PUBLIC ../../shared/include)
target_link_libraries(falafel_pairing PUBLIC noise_protocol)
add_library(falafel_e2ee STATIC ../../shared/e2ee/falafel_e2ee.c)
target_link_libraries(falafel_e2ee PUBLIC falafel_pairing)
add_executable(test_e2ee test_e2ee.c)
# Compile the implementation into this test only, so it can place the Noise
# nonce and corresponding sequence at the exhaustion boundary without exposing
# a nonce-setting API in the production library.
target_link_libraries(test_e2ee PRIVATE falafel_pairing)

add_executable(pairing_peer pairing_peer.c)
target_link_libraries(pairing_peer PRIVATE falafel_pairing)
add_executable(pairing_harness pairing_harness.c)
target_link_libraries(pairing_harness PRIVATE falafel_pairing)
add_executable(test_pairing test_pairing.c)
target_link_libraries(test_pairing PRIVATE falafel_pairing)
add_library(pairing_store_macos STATIC pairing_store_macos.c)
target_link_libraries(pairing_store_macos PUBLIC falafel_pairing "-framework Security" "-framework CoreFoundation")
target_include_directories(pairing_store_macos PUBLIC .)
add_executable(test_pairing_persistence test_pairing_persistence.c)
target_link_libraries(test_pairing_persistence PRIVATE pairing_store_macos)
add_executable(pairing_persistence_harness pairing_persistence_harness.c)
target_link_libraries(pairing_persistence_harness PRIVATE pairing_store_macos falafel_e2ee)

target_compile_options(noise_protocol PRIVATE -Wall -Wextra)
foreach(target falafel_pairing falafel_e2ee test_e2ee pairing_peer pairing_harness test_pairing pairing_store_macos test_pairing_persistence pairing_persistence_harness)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endforeach()
target_compile_options(test_pairing PRIVATE -UNDEBUG)
target_compile_options(test_e2ee PRIVATE -UNDEBUG)
target_compile_options(test_pairing_persistence PRIVATE -UNDEBUG)

if(FALAFEL_ENABLE_SANITIZERS)
    set(SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    foreach(target noise_protocol falafel_pairing falafel_e2ee test_e2ee pairing_peer pairing_harness test_pairing pairing_store_macos test_pairing_persistence pairing_persistence_harness)
        target_compile_options(${target} PRIVATE ${SAN_FLAGS})
    endforeach()
    foreach(target pairing_peer pairing_harness test_pairing test_e2ee test_pairing_persistence pairing_persistence_harness)
        target_link_options(${target} PRIVATE ${SAN_FLAGS})
    endforeach()
endif()

enable_testing()
add_test(NAME pairing_unit COMMAND test_pairing)
add_test(NAME pairing_two_processes COMMAND pairing_harness)
add_test(NAME pairing_persistence_unit COMMAND test_pairing_persistence)
add_test(NAME pairing_persistence_restart COMMAND pairing_persistence_harness)
add_test(NAME e2ee_unit COMMAND test_e2ee)
