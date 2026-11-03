#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH 0x1103
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE 0x1105
#define ESP_ERR_NVS_REMOVE_FAILED 0x1108
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_U8 = 1, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff } nvs_type_t;
typedef struct { char namespace_name[16], key[16]; nvs_type_t type; } nvs_entry_info_t;
typedef struct nvs_opaque_iterator_t *nvs_iterator_t;
esp_err_t nvs_open_from_partition(const char *, const char *, nvs_open_mode_t, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_entry_find(const char *, const char *, nvs_type_t, nvs_iterator_t *);
esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *);
esp_err_t nvs_entry_next(nvs_iterator_t *);
void nvs_release_iterator(nvs_iterator_t);
