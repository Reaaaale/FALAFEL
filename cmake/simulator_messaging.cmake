# Reuse the existing mailbox runtime without importing the root test project.
include_guard(GLOBAL)
include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(mbedtls)
set(FALAFEL_REPO "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(simulator_messaging STATIC
    ${FALAFEL_REPO}/simulator/src/messaging_controller.c
    ${FALAFEL_REPO}/simulator/src/desktop_transport.c
    ${FALAFEL_REPO}/shared/crypto/falafel_crypto_mbedtls.c
    ${FALAFEL_REPO}/shared/e2ee/falafel_async_e2ee.c
    ${FALAFEL_REPO}/shared/e2ee/falafel_mailbox.c
    ${FALAFEL_REPO}/shared/e2ee/falafel_mail_link.c
    ${FALAFEL_REPO}/shared/storage/falafel_mailbox_posix.c
    ${FALAFEL_REPO}/shared/storage/falafel_history.c
    ${FALAFEL_REPO}/shared/storage/falafel_history_posix.c
    # Transport's unused v1/state helpers retain these link dependencies.
    ${FALAFEL_REPO}/shared/app/falafel_app.c
    ${FALAFEL_REPO}/shared/app/falafel_protocol.c)
target_include_directories(simulator_messaging PUBLIC
    ${FALAFEL_REPO}/shared/include ${FALAFEL_REPO}/simulator/src)
target_compile_features(simulator_messaging PUBLIC c_std_11)
target_compile_options(simulator_messaging PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(simulator_messaging PUBLIC MbedTLS::mbedcrypto Threads::Threads)
