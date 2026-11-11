#include "esp_log.h"
#include "falafel_storage_espidf.h"
#include "relay_network.h"

static const char *TAG = "falafel";
static falafel_storage_espidf storage;

void app_main(void)
{
    ESP_LOGI(TAG, "Falafel firmware started");
    ESP_LOGW(TAG, "Lab identities only; optional E2EE mailbox worker, no TLS/mTLS");
    /* The probe never creates storage. The optional lab worker separately owns
     * a provisioned identity and explicitly created/reopened mailbox. */
    esp_err_t error = falafel_storage_espidf_open(&storage, "falafel_data", false);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Existing storage namespace opened; application records not yet authenticated");
        error = falafel_storage_espidf_close(&storage);
    } else if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Storage unprovisioned; explicit initialization required");
    }
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGE(TAG, "Storage unavailable: %s; never erased automatically", esp_err_to_name(error));
    /* Transport preparation is independent of application storage provisioning. */
    falafel_relay_network_start();
}

