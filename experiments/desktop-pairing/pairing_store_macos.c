#include "pairing_store_macos.h"

#include <Security/Security.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static CFStringRef string_ref(const char *text)
{
    return text ? CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8) : NULL;
}

static int keychain_read(const falafel_pairing_macos_store *store, uint8_t key[32])
{
    CFMutableDictionaryRef query = NULL; CFStringRef service = NULL, account = NULL;
    CFTypeRef result = NULL; OSStatus status; int rc = FALAFEL_STORE_ERROR;
    service = string_ref(store->keychain_service); account = string_ref(store->keychain_account);
    query = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    if (!service || !account || !query) goto done;
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    status = SecItemCopyMatching(query, &result);
    if (status == errSecItemNotFound) { rc = 1; goto done; }
    if (status != errSecSuccess || !result || CFGetTypeID(result) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)result) != 32) goto done;
    memcpy(key, CFDataGetBytePtr((CFDataRef)result), 32); rc = 0;
done:
    if (result) CFRelease(result); if (query) CFRelease(query);
    if (service) CFRelease(service); if (account) CFRelease(account); return rc;
}

static int keychain_add(const falafel_pairing_macos_store *store, const uint8_t key[32])
{
    CFMutableDictionaryRef item = NULL; CFStringRef service = NULL, account = NULL;
    CFDataRef data = NULL; OSStatus status; int rc = -1;
    service = string_ref(store->keychain_service); account = string_ref(store->keychain_account);
    data = CFDataCreate(NULL, key, 32);
    item = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
    if (!service || !account || !data || !item) goto done;
    CFDictionarySetValue(item, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(item, kSecAttrService, service);
    CFDictionarySetValue(item, kSecAttrAccount, account);
    CFDictionarySetValue(item, kSecValueData, data);
    CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
    status = SecItemAdd(item, NULL); rc = status == errSecSuccess ? 0 : -1;
done:
    if (item) CFRelease(item); if (data) CFRelease(data);
    if (service) CFRelease(service); if (account) CFRelease(account); return rc;
}

static int keychain_delete(const falafel_pairing_macos_store *store)
{
    CFMutableDictionaryRef query = NULL; CFStringRef service = NULL, account = NULL;
    OSStatus status; int rc = -1;
    service = string_ref(store->keychain_service); account = string_ref(store->keychain_account);
    query = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    if (!service || !account || !query) goto done;
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    status = SecItemDelete(query); rc = (status == errSecSuccess || status == errSecItemNotFound) ? 0 : -1;
done:
    if (query) CFRelease(query); if (service) CFRelease(service); if (account) CFRelease(account); return rc;
}

static int sync_parent(const char *path)
{
    char parent[PATH_MAX], *slash; int fd, rc;
    if (strlen(path) >= sizeof(parent)) return -1;
    strcpy(parent, path); slash = strrchr(parent, '/');
    if (slash) { if (slash == parent) slash[1] = '\0'; else *slash = '\0'; }
    else strcpy(parent, ".");
    fd = open(parent, O_RDONLY); if (fd < 0) return -1;
    rc = fsync(fd); if (close(fd) && !rc) rc = -1; return rc;
}

static int atomic_write(const char *path, const uint8_t data[88], int interrupt)
{
    char temporary[PATH_MAX]; int fd = -1, rc = -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >= (int)sizeof(temporary)) return -1;
    fd = mkstemp(temporary);
    if (fd < 0) return -1;
    if (fchmod(fd, S_IRUSR | S_IWUSR) || write(fd, data, 88) != 88 || fsync(fd) || close(fd)) {
        fd = -1; goto done;
    }
    fd = -1;
    if (interrupt) { unlink(temporary); return FALAFEL_STORE_ERROR; }
    if (rename(temporary, path) || sync_parent(path)) goto done;
    return 0;
done:
    if (fd >= 0) close(fd); unlink(temporary); return rc;
}

static int read_metadata(const char *path, uint8_t data[88])
{
    uint8_t extra; int fd = open(path, O_RDONLY); ssize_t count;
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    count = read(fd, data, 88);
    if (count != 88 || read(fd, &extra, 1) != 0) { close(fd); return -1; }
    return close(fd) ? -1 : 0;
}

static int save_internal(const falafel_pairing_macos_store *store,
                         const falafel_pairing_persistent_state *state, int interrupt)
{
    uint8_t existing[32], encoded[88]; int found;
    if (!store || !store->metadata_path || !store->keychain_service || !store->keychain_account ||
        falafel_pairing_persistent_validate(state) || falafel_pairing_persistent_encode(state, encoded)) return -1;
    found = keychain_read(store, existing);
    if (found < 0 || (found == 0 && memcmp(existing, state->local_private, 32))) { memset(existing, 0, 32); return -1; }
    if (found == 1 && keychain_add(store, state->local_private)) return -1;
    memset(existing, 0, 32);
    return atomic_write(store->metadata_path, encoded, interrupt);
}

int falafel_pairing_macos_save(const falafel_pairing_macos_store *store,
                               const falafel_pairing_persistent_state *state)
{
    return save_internal(store, state, 0);
}

int falafel_pairing_macos_simulate_interrupted_save(const falafel_pairing_macos_store *store,
                                                     const falafel_pairing_persistent_state *state)
{
    return save_internal(store, state, 1);
}

int falafel_pairing_macos_load_or_create(const falafel_pairing_macos_store *store,
                                         falafel_pairing_persistent_state *state)
{
    uint8_t key[32], metadata[88]; int key_status, file_status, rc;
    if (!store || !state) return FALAFEL_STORE_ERROR;
    key_status = keychain_read(store, key); file_status = read_metadata(store->metadata_path, metadata);
    if (key_status < 0 || file_status < 0) { memset(key, 0, 32); return FALAFEL_STORE_CORRUPT; }
    if (key_status == 0 && file_status == 0) {
        rc = falafel_pairing_persistent_decode(state, key, metadata, 88);
        memset(key, 0, 32); return rc ? FALAFEL_STORE_CORRUPT : FALAFEL_STORE_OK;
    }
    if (key_status == 1 && file_status == 0) return FALAFEL_STORE_CORRUPT;
    if (key_status == 1 && falafel_pairing_generate_identity(key)) return FALAFEL_STORE_ERROR;
    if (falafel_pairing_persistent_init(state, key)) { memset(key, 0, 32); return FALAFEL_STORE_ERROR; }
    memset(key, 0, 32);
    if (falafel_pairing_macos_save(store, state)) { falafel_pairing_persistent_clear(state); return FALAFEL_STORE_ERROR; }
    return FALAFEL_STORE_OK;
}

int falafel_pairing_macos_unpair(const falafel_pairing_macos_store *store, int rotate,
                                 falafel_pairing_persistent_state *state)
{
    uint8_t key[32];
    if (!store || !state) return -1;
    if (unlink(store->metadata_path) < 0 && errno != ENOENT) return -1;
    if (sync_parent(store->metadata_path)) return -1;
    if (rotate && keychain_delete(store)) return -1;
    if (rotate) {
        if (falafel_pairing_generate_identity(key)) return -1;
    } else if (keychain_read(store, key) != 0) return -1;
    falafel_pairing_persistent_clear(state);
    if (falafel_pairing_persistent_init(state, key)) { memset(key, 0, 32); return -1; }
    memset(key, 0, 32); return falafel_pairing_macos_save(store, state);
}

int falafel_pairing_macos_destroy(const falafel_pairing_macos_store *store)
{
    if (!store || ((unlink(store->metadata_path) < 0) && errno != ENOENT)) return -1;
    return keychain_delete(store);
}
