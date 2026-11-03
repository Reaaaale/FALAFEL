#pragma once
#include "nvs.h"
enum fake_cut {
    FAKE_NONE, FAKE_BEFORE_SET, FAKE_PARTIAL_UNPUBLISHED,
    FAKE_AFTER_SET, FAKE_BEFORE_COMMIT, FAKE_AFTER_COMMIT
};
void fake_nvs_reset(void);
void fake_nvs_reboot(void);
void fake_nvs_fault(unsigned write, enum fake_cut);
void fake_nvs_init_error(esp_err_t);
void fake_nvs_capacity(size_t);
void fake_nvs_raw(const char *key, nvs_type_t, const uint8_t *, size_t);
void fake_nvs_delete(const char *key);
void fake_nvs_flip(const char *key, size_t offset);
const uint8_t *fake_nvs_blob(const char *key, size_t *);
unsigned fake_nvs_writes(void);
unsigned fake_nvs_keys(void);
