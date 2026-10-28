set(NOISE_EXPERIMENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../noise-feasibility")
include("${NOISE_EXPERIMENT_DIR}/noise_sources.cmake")
idf_component_register(
    SRCS "main.c" "noise_feasibility_esp32.c" ${NOISE_PROTOCOL_SOURCES}
    INCLUDE_DIRS "${NOISE_C_SOURCE_DIR}/include"
    PRIV_INCLUDE_DIRS "${NOISE_C_SOURCE_DIR}/src" "${NOISE_C_SOURCE_DIR}/src/protocol"
    REQUIRES esp_hw_support esp_timer heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_OPENSSL=0 USE_LIBSODIUM=0)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -UNDEBUG)
