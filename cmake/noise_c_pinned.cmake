include_guard(GLOBAL)

set(FALAFEL_NOISE_C_REVISION
    "cfe25410979a87391bb9ac8d4d4bef64e9f268c6")
set(FALAFEL_NOISE_C_ARCHIVE_SHA256
    "d95cdd666124e9d1175c0c24024d7cabcd71149a9dbfe0de3aca43c562f7ac12")

if(NOT NOISE_C_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(noise_c
        URL "https://github.com/rweather/noise-c/archive/${FALAFEL_NOISE_C_REVISION}.tar.gz"
        URL_HASH "SHA256=${FALAFEL_NOISE_C_ARCHIVE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(noise_c)
    set(NOISE_C_SOURCE_DIR "${noise_c_SOURCE_DIR}" CACHE PATH
        "Pinned Noise-C source tree" FORCE)
endif()

if(NOT EXISTS "${NOISE_C_SOURCE_DIR}/include/noise/protocol.h")
    message(FATAL_ERROR
        "NOISE_C_SOURCE_DIR does not contain the pinned Noise-C source tree: ${NOISE_C_SOURCE_DIR}")
endif()
