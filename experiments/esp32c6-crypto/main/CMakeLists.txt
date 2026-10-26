idf_component_register(
    SRCS "main.c" "../../../shared/crypto/falafel_crypto_mbedtls.c"
    INCLUDE_DIRS "../../../shared/include"
    REQUIRES mbedtls esp_timer heap
)
