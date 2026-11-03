idf_component_register(SRCS "falafel_storage_espidf.c"
    INCLUDE_DIRS "include" REQUIRES falafel_persistence nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
