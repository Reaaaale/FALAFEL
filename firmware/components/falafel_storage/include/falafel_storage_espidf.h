#pragma once

#include "falafel_history.h"
#include "esp_err.h"
#include "nvs.h"
#include <stdbool.h>

/* One dedicated NVS partition, one mailbox/archive pair. Zero-initialize before
 * first open. Caller serializes ALL operations (including close) in its storage
 * task; never call these synchronous flash operations from the LVGL task.
 * Only one backend owner is permitted in this process. No keys/identities here.
 * The original FMBX/FHI1/FHR1 bytes are stored without an extra wire format. */
typedef struct {
    nvs_handle_t handle;
    char partition[17];
    /* Direction 0xff denotes an unused physical slot. Full IDs, no truncation. */
    uint8_t records[FALAFEL_HISTORY_CAPACITY][17];
    esp_err_t last_error;
    bool claimed, initialized, opened, poisoned, mailbox_present, index_present;
} falafel_storage_espidf;

/* create_namespace is an explicit provisioning operation, NOT normal startup.
 * It does not initialize a mailbox/history: their existing explicit create
 * flags still apply. Never retries errors by erasing or formatting a partition.
 * Partition must be dedicated, labeled other than "nvs", and not initialized
 * or accessed by other application components. Close before reopening on error.
 * Built for disposable data only until embedded crypto/key protection is verified. */
esp_err_t falafel_storage_espidf_open(falafel_storage_espidf *,
                                    const char *partition, bool create_namespace);
esp_err_t falafel_storage_espidf_close(falafel_storage_espidf *);
falafel_mail_storage falafel_storage_espidf_mailbox(falafel_storage_espidf *);
falafel_history_storage falafel_storage_espidf_history(falafel_storage_espidf *);
