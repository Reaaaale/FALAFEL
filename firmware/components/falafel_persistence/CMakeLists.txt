# Portable codecs and crypto are unchanged; no POSIX adapter or GUI dependency.
set(FALAFEL_SHARED "${CMAKE_CURRENT_LIST_DIR}/../../../shared")
idf_component_register(SRCS
    "${FALAFEL_SHARED}/crypto/falafel_crypto_mbedtls.c"
    "${FALAFEL_SHARED}/e2ee/falafel_async_e2ee.c"
    "${FALAFEL_SHARED}/e2ee/falafel_mailbox.c"
    "${FALAFEL_SHARED}/e2ee/falafel_mail_link.c"
    "${FALAFEL_SHARED}/storage/falafel_history.c"
    INCLUDE_DIRS "${FALAFEL_SHARED}/include" REQUIRES mbedtls)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
