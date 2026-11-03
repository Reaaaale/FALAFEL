idf_component_register(
    SRCS "main.c" "sec003_suite.c" "sec003_probe.c"
        "../../../shared/crypto/falafel_crypto_mbedtls.c"
    INCLUDE_DIRS "../../../shared/include"
    REQUIRES mbedtls esp_timer heap bootloader_support esp_app_format
)
# Experiment-only callback probe. The production provider FILE is unchanged;
# this definition is restricted to its object in this experiment, not the proxy.
set_property(SOURCE "../../../shared/crypto/falafel_crypto_mbedtls.c" APPEND
    PROPERTY COMPILE_DEFINITIONS mbedtls_entropy_func=sec003_entropy_func)
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=mbedtls_hardware_poll")
