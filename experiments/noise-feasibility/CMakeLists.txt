cmake_minimum_required(VERSION 3.16)
project(falafel_noise_feasibility C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(../../cmake/noise_c_pinned.cmake)

include(noise_sources.cmake)
add_library(noise_protocol STATIC ${NOISE_PROTOCOL_SOURCES})
target_include_directories(noise_protocol PUBLIC "${NOISE_C_SOURCE_DIR}/include")
target_include_directories(noise_protocol PRIVATE
    "${NOISE_C_SOURCE_DIR}/src"
    "${NOISE_C_SOURCE_DIR}/src/protocol")
target_compile_definitions(noise_protocol PRIVATE USE_OPENSSL=0 USE_LIBSODIUM=0)
target_compile_options(noise_protocol PRIVATE -Wall -Wextra)

add_executable(noise_feasibility noise_feasibility.c)
target_link_libraries(noise_feasibility PRIVATE noise_protocol)
target_compile_options(noise_feasibility PRIVATE -Wall -Wextra -Werror -UNDEBUG)

if(FALAFEL_ENABLE_SANITIZERS)
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(noise_protocol PRIVATE ${SANITIZER_FLAGS})
    target_compile_options(noise_feasibility PRIVATE ${SANITIZER_FLAGS})
    target_link_options(noise_feasibility PRIVATE ${SANITIZER_FLAGS})
endif()

enable_testing()
add_test(NAME noise_feasibility COMMAND noise_feasibility)
