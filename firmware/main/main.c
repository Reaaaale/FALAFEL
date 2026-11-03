#include "esp_log.h"
#include "falafel_storage_espidf.h"

static const char *TAG = "falafel";
static falafel_storage_espidf storage;

void app_main(void)
{
    ESP_LOGI(TAG, "Falafel firmware started");
    ESP_LOGW(TAG, "Storage experiment only: embedded crypto is not approved for real identities/data");
    /* Probe only: no namespace, history, mailbox or identity is auto-created.
     * This stub has no messaging consumer. A future worker owns this handle. */
    esp_err_t error = falafel_storage_espidf_open(&storage, "falafel_data", false);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Existing storage namespace opened; application records not yet authenticated");
        error = falafel_storage_espidf_close(&storage);
    } else if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Storage unprovisioned; explicit initialization required");
        return;
    }
    if (error != ESP_OK) ESP_LOGE(TAG, "Storage unavailable: %s; never erased automatically", esp_err_to_name(error));
}

