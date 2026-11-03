/* Deliberately small NVS API model. It injects OLD/NEW outcomes around a single
 * blob publication, including errors after publication and invisible fragments.
 * It does NOT model NVS page CRCs, flash programming, erasure, GC or wear.
 * Physical power-cut testing against the pinned ESP-IDF is still mandatory. */
#include "nvs_model.h"
#include "nvs_flash.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KEYS 1030
typedef struct { char key[16]; uint8_t *bytes; size_t n; nvs_type_t type; } entry;
static entry entries[MAX_KEYS], pending;
static bool initialized, namespace_exists, handle_open;
static esp_err_t init_error;
static unsigned writes, fail_write;
static enum fake_cut cut;
static size_t capacity;
struct nvs_opaque_iterator_t { unsigned slot; };

static int lookup(const char *key)
{
    for (unsigned i = 0; i < MAX_KEYS; ++i)
        if (entries[i].bytes && !strcmp(entries[i].key, key)) return (int)i;
    return -1;
}
static void discard(void) { free(pending.bytes); memset(&pending, 0, sizeof(pending)); }
static void publish(void)
{
    assert(pending.bytes);
    int i = lookup(pending.key);
    if (i < 0) for (unsigned j = 0; j < MAX_KEYS; ++j)
        if (!entries[j].bytes) { i = (int)j; break; }
    assert(i >= 0);
    free(entries[i].bytes); entries[i] = pending; memset(&pending, 0, sizeof(pending));
}
void fake_nvs_reboot(void)
{
    discard(); initialized = handle_open = false;
    fail_write = 0; cut = FAKE_NONE; init_error = ESP_OK;
}
void fake_nvs_reset(void)
{
    fake_nvs_reboot();
    for (unsigned i = 0; i < MAX_KEYS; ++i) free(entries[i].bytes);
    memset(entries, 0, sizeof(entries)); namespace_exists = false;
    capacity = 2 * 1024 * 1024; writes = 0;
}
void fake_nvs_fault(unsigned write, enum fake_cut outcome)
{ fail_write = writes + write; cut = outcome; }
void fake_nvs_init_error(esp_err_t error) { init_error = error; }
void fake_nvs_capacity(size_t n) { capacity = n; }
unsigned fake_nvs_writes(void) { return writes; }
unsigned fake_nvs_keys(void)
{ unsigned n = 0; for (unsigned i = 0; i < MAX_KEYS; ++i) n += entries[i].bytes != NULL; return n; }
void fake_nvs_raw(const char *key, nvs_type_t type, const uint8_t *bytes, size_t n)
{
    assert(!pending.bytes && strlen(key) < 16);
    pending.bytes = malloc(n ? n : 1); assert(pending.bytes);
    memcpy(pending.bytes, bytes, n); pending.n = n; pending.type = type;
    strcpy(pending.key, key); publish(); namespace_exists = true;
}
void fake_nvs_delete(const char *key)
{ int i = lookup(key); assert(i >= 0); free(entries[i].bytes); memset(&entries[i], 0, sizeof(entries[i])); }
void fake_nvs_flip(const char *key, size_t offset)
{ int i = lookup(key); assert(i >= 0 && offset < entries[i].n); entries[i].bytes[offset] ^= 1; }
const uint8_t *fake_nvs_blob(const char *key, size_t *n)
{ int i = lookup(key); if (i < 0) return NULL; *n = entries[i].n; return entries[i].bytes; }

esp_err_t nvs_flash_init_partition(const char *partition)
{
    assert(partition && strcmp(partition, "nvs"));
    if (init_error) return init_error;
    assert(!initialized); initialized = true; return ESP_OK;
}
esp_err_t nvs_flash_deinit_partition(const char *partition)
{ (void)partition; assert(initialized && !handle_open); initialized = false; return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *partition, const char *name,
                                nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)partition; assert(initialized && !handle_open && !strcmp(name, "falafel"));
    if (!namespace_exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    namespace_exists = true; handle_open = true; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle)
{ assert(handle == 1 && handle_open); discard(); handle_open = false; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *n)
{
    assert(handle == 1 && handle_open && initialized);
    int i = lookup(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    if (entries[i].type != NVS_TYPE_BLOB) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (out && *n < entries[i].n) { *n = entries[i].n; return ESP_ERR_NVS_INVALID_LENGTH; }
    *n = entries[i].n;
    if (out) memcpy(out, entries[i].bytes, *n);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t n)
{
    assert(handle == 1 && handle_open && initialized && !pending.bytes);
    ++writes;
    enum fake_cut fault = writes == fail_write ? cut : FAKE_NONE;
    if (fault == FAKE_BEFORE_SET) return ESP_FAIL;
    size_t used = 0;
    for (unsigned i = 0; i < MAX_KEYS; ++i) used += entries[i].n;
    /* Conservative live-byte model with room for old AND new blob. */
    if (used + n > capacity) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    pending.bytes = malloc(n); assert(pending.bytes);
    memcpy(pending.bytes, data, n); pending.n = n; pending.type = NVS_TYPE_BLOB;
    assert(strlen(key) < sizeof(pending.key)); strcpy(pending.key, key);
    if (fault == FAKE_PARTIAL_UNPUBLISHED) { discard(); return ESP_FAIL; }
    if (fault == FAKE_AFTER_SET) { publish(); return ESP_ERR_NVS_REMOVE_FAILED; }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && handle_open && initialized && pending.bytes);
    enum fake_cut fault = writes == fail_write ? cut : FAKE_NONE;
    if (fault == FAKE_BEFORE_COMMIT) return ESP_FAIL;
    publish(); return fault == FAKE_AFTER_COMMIT ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_entry_find(const char *partition, const char *name, nvs_type_t type, nvs_iterator_t *it)
{
    (void)partition; assert(initialized && !strcmp(name, "falafel") && type == NVS_TYPE_ANY);
    unsigned i = 0; while (i < MAX_KEYS && !entries[i].bytes) ++i;
    *it = NULL; if (i == MAX_KEYS) return ESP_ERR_NVS_NOT_FOUND;
    *it = malloc(sizeof(**it)); assert(*it); (*it)->slot = i; return ESP_OK;
}
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *info)
{
    assert(it && entries[it->slot].bytes); strcpy(info->namespace_name, "falafel");
    strcpy(info->key, entries[it->slot].key); info->type = entries[it->slot].type;
    return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t *it)
{
    assert(*it); unsigned i = (*it)->slot + 1;
    while (i < MAX_KEYS && !entries[i].bytes) ++i;
    if (i == MAX_KEYS) { nvs_release_iterator(*it); *it = NULL; return ESP_ERR_NVS_NOT_FOUND; }
    (*it)->slot = i; return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it) { free(it); }
