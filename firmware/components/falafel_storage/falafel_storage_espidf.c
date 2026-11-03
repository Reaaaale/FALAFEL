#include "falafel_storage_espidf.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define STORE_NAMESPACE "falafel"
static atomic_flag owner = ATOMIC_FLAG_INIT;

static int fail(falafel_storage_espidf *s, esp_err_t error)
{
    if (s && !s->poisoned) { s->last_error = error; s->poisoned = true; }
    return -1;
}

static bool usable(const falafel_storage_espidf *s)
{ return s && s->claimed && s->opened && !s->poisoned; }

static void slot_key(unsigned slot, char key[6])
{ (void)snprintf(key, 6, "r%04u", slot); }

static int parse_slot(const char *key)
{
    if (strlen(key) != 5 || key[0] != 'r') return -1;
    unsigned slot = 0;
    for (unsigned i = 1; i < 5; ++i) {
        if (key[i] < '0' || key[i] > '9') return -1;
        slot = slot * 10 + (unsigned)(key[i] - '0');
    }
    return slot < FALAFEL_HISTORY_CAPACITY ? (int)slot : -1;
}

static int parse_name(const char *name, uint8_t logical[17])
{
    if (!name || strlen(name) != 38 || (name[0] != '0' && name[0] != '1') ||
        name[1] != '-' || strcmp(name + 34, ".msg")) return -1;
    logical[0] = (uint8_t)(name[0] - '0');
    for (unsigned i = 0; i < 32; ++i) {
        char c = name[i + 2];
        unsigned nibble;
        if (c >= '0' && c <= '9') nibble = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = (unsigned)(c - 'a') + 10;
        else return -1;
        if (!(i & 1)) logical[1 + i / 2] = (uint8_t)(nibble << 4);
        else logical[1 + i / 2] |= (uint8_t)nibble;
    }
    return 0;
}

static void logical_name(const uint8_t logical[17], char name[39])
{
    static const char hex[] = "0123456789abcdef";
    name[0] = (char)('0' + logical[0]); name[1] = '-';
    for (unsigned i = 0; i < 16; ++i) {
        name[2 + i * 2] = hex[logical[i + 1] >> 4];
        name[3 + i * 2] = hex[logical[i + 1] & 15];
    }
    memcpy(name + 34, ".msg", 5);
}

static int find_record(const falafel_storage_espidf *s, const uint8_t logical[17])
{
    for (unsigned i = 0; i < FALAFEL_HISTORY_CAPACITY; ++i)
        if (!memcmp(s->records[i], logical, 17)) return (int)i;
    return -1;
}

static int read_blob(falafel_storage_espidf *s, const char *key,
                     uint8_t *out, size_t cap, size_t *size, bool present)
{
    if (!usable(s) || !out || !size) return -1;
    *size = 0;
    size_t n = 0;
    esp_err_t error = nvs_get_blob(s->handle, key, NULL, &n);
    if (error == ESP_ERR_NVS_NOT_FOUND && !present) return 1;
    if (error != ESP_OK) return fail(s, error);
    if (n > cap || !n) return fail(s, ESP_ERR_NVS_INVALID_LENGTH);
    *size = n;
    error = nvs_get_blob(s->handle, key, out, size);
    if (error != ESP_OK) return fail(s, error);
    if (*size != n) return fail(s, ESP_ERR_NVS_INVALID_LENGTH);
    return 0;
}

static int write_blob(falafel_storage_espidf *s, const char *key,
                      const uint8_t *data, size_t n)
{
    if (!usable(s) || !data) return -1;
    /* Not a multi-key transaction. set_blob may already reach flash; even an
     * error may have published the new blob. Poison and rebuild on reopen. */
    esp_err_t error = nvs_set_blob(s->handle, key, data, n);
    if (error == ESP_OK) error = nvs_commit(s->handle);
    return error == ESP_OK ? 0 : fail(s, error);
}

static int mail_load(void *user, uint8_t *out, size_t cap, size_t *n)
{
    falafel_storage_espidf *s = user;
    return read_blob(s, "mailbox", out, cap, n, s && s->mailbox_present);
}

static int mail_replace(void *user, const uint8_t *data, size_t n)
{
    falafel_storage_espidf *s = user;
    if (n != FALAFEL_MAILBOX_SNAPSHOT_SIZE && n != FALAFEL_MAILBOX_MAX_SNAPSHOT_SIZE)
        return fail(s, ESP_ERR_INVALID_SIZE);
    if (write_blob(s, "mailbox", data, n)) return -1;
    s->mailbox_present = true;
    return 0;
}

static int history_load(void *user, const char *name, uint8_t *out, size_t cap, size_t *n)
{
    falafel_storage_espidf *s = user;
    if (!usable(s) || !name || !out || !n) return -1;
    if (!strcmp(name, "index"))
        return read_blob(s, "index", out, cap, n, s->index_present);
    uint8_t logical[17];
    if (parse_name(name, logical)) return fail(s, ESP_ERR_INVALID_ARG);
    int slot = find_record(s, logical);
    if (slot < 0) { *n = 0; return 1; }
    char key[6]; slot_key((unsigned)slot, key);
    if (read_blob(s, key, out, cap, n, true)) return -1;
    if (*n != FALAFEL_HISTORY_RECORD_BYTES || memcmp(out, "FHR1", 4) ||
        memcmp(out + 20, logical, 17)) return fail(s, ESP_ERR_INVALID_STATE);
    return 0;
}

static int history_replace(void *user, const char *name, const uint8_t *data, size_t n)
{
    falafel_storage_espidf *s = user;
    if (!usable(s) || !name || !data) return -1;
    if (!strcmp(name, "index")) {
        if (n < 60 || n > FALAFEL_HISTORY_INDEX_BYTES || (n - 60) % 17)
            return fail(s, ESP_ERR_INVALID_SIZE);
        if (write_blob(s, "index", data, n)) return -1;
        s->index_present = true;
        return 0;
    }
    uint8_t logical[17];
    if (parse_name(name, logical) || n != FALAFEL_HISTORY_RECORD_BYTES ||
        memcmp(data, "FHR1", 4) || memcmp(data + 20, logical, 17))
        return fail(s, ESP_ERR_INVALID_ARG);
    int slot = find_record(s, logical);
    if (slot < 0) {
        for (unsigned i = 0; i < FALAFEL_HISTORY_CAPACITY; ++i)
            if (s->records[i][0] == 0xff) { slot = (int)i; break; }
    }
    if (slot < 0) return fail(s, ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    char key[6]; slot_key((unsigned)slot, key);
    if (write_blob(s, key, data, n)) return -1;
    memcpy(s->records[slot], logical, 17);
    return 0;
}

static int history_scan(void *user, falafel_history_visit visit, void *context)
{
    falafel_storage_espidf *s = user;
    if (!usable(s) || !visit) return -1;
    /* No live NVS iterator while the visitor recovers/commits the manifest. */
    for (unsigned i = 0; i < FALAFEL_HISTORY_CAPACITY; ++i) {
        if (s->records[i][0] == 0xff) continue;
        char name[39]; logical_name(s->records[i], name);
        if (visit(context, name)) return -1;
    }
    return 0;
}

static esp_err_t rebuild(falafel_storage_espidf *s)
{
    nvs_iterator_t iterator = NULL;
    esp_err_t error = nvs_entry_find(s->partition, STORE_NAMESPACE, NVS_TYPE_ANY, &iterator);
    while (error == ESP_OK) {
        nvs_entry_info_t info;
        error = nvs_entry_info(iterator, &info);
        if (error != ESP_OK) break;
        if (info.type != NVS_TYPE_BLOB) { error = ESP_ERR_NVS_TYPE_MISMATCH; break; }
        if (!strcmp(info.key, "index")) s->index_present = true;
        else if (!strcmp(info.key, "mailbox")) s->mailbox_present = true;
        else {
            int slot = parse_slot(info.key);
            uint8_t record[FALAFEL_HISTORY_RECORD_BYTES]; size_t n = sizeof(record);
            if (slot < 0) { error = ESP_ERR_INVALID_STATE; break; }
            error = nvs_get_blob(s->handle, info.key, record, &n);
            if (error != ESP_OK) break;
            if (n != sizeof(record) || memcmp(record, "FHR1", 4) || record[20] > 1 ||
                s->records[slot][0] != 0xff || find_record(s, record + 20) >= 0) {
                error = ESP_ERR_INVALID_STATE; break;
            }
            memcpy(s->records[slot], record + 20, 17);
        }
        error = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
}

esp_err_t falafel_storage_espidf_open(falafel_storage_espidf *s,
                                    const char *partition, bool create_namespace)
{
    if (!s || !partition || !*partition || strlen(partition) > 16 || !strcmp(partition, "nvs"))
        return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set_explicit(&owner, memory_order_acquire))
        return ESP_ERR_INVALID_STATE;
    memset(s, 0, sizeof(*s)); s->claimed = true;
    memset(s->records, 0xff, sizeof(s->records)); strcpy(s->partition, partition);
    esp_err_t error = nvs_flash_init_partition(partition);
    if (error != ESP_OK) goto failed;
    s->initialized = true;
    /* Check existence without creating it on ordinary boots. */
    error = nvs_open_from_partition(partition, STORE_NAMESPACE, NVS_READONLY, &s->handle);
    if (error == ESP_OK) nvs_close(s->handle);
    else if (error != ESP_ERR_NVS_NOT_FOUND || !create_namespace) goto failed;
    error = nvs_open_from_partition(partition, STORE_NAMESPACE, NVS_READWRITE, &s->handle);
    if (error != ESP_OK) goto failed;
    s->opened = true;
    error = rebuild(s);
    if (error != ESP_OK) goto failed;
    return ESP_OK;
failed:
    (void)falafel_storage_espidf_close(s);
    s->last_error = error; s->poisoned = true;
    return error;
}

esp_err_t falafel_storage_espidf_close(falafel_storage_espidf *s)
{
    if (!s || !s->claimed) return ESP_ERR_INVALID_STATE;
    if (s->opened) nvs_close(s->handle);
    esp_err_t error = s->initialized ? nvs_flash_deinit_partition(s->partition) : ESP_OK;
    s->opened = false; s->initialized = false; s->claimed = false;
    memset(s->records, 0xff, sizeof(s->records));
    atomic_flag_clear_explicit(&owner, memory_order_release);
    return error;
}

falafel_mail_storage falafel_storage_espidf_mailbox(falafel_storage_espidf *s)
{ falafel_mail_storage io = {s, mail_load, mail_replace}; return io; }
falafel_history_storage falafel_storage_espidf_history(falafel_storage_espidf *s)
{ falafel_history_storage io = {s, history_load, history_replace, history_scan}; return io; }
