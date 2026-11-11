#include "relay_network.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "falafel_network";
#ifdef CONFIG_FALAFEL_NETWORK_ENABLE
#include "relay_tcp.h"
#include "lab_messaging.h"
#include "falafel_storage_espidf.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define WIFI_READY BIT0
static EventGroupHandle_t wifi_events;

static void lab_event(void *user, falafel_lab_event event, const uint8_t id[16])
{
    (void)user;
    static const char *names[] = {"STORED", "RECEIVED", "DELIVERED_AUTHENTICATED", "RESTORED", "REJECTED"};
    char hex[33];
    for (unsigned i = 0; i < 16; ++i) snprintf(hex + 2*i, 3, "%02x", id[i]);
    ESP_LOGI(TAG, "E2EE %s id=%s", names[event], hex);
}

static void resources(void)
{
    ESP_LOGI(TAG, "RESOURCES free_heap=%u minimum_heap=%u largest_block=%u stack_min=%u",
        (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static bool load_credential(uint8_t credential[64])
{
    uint8_t record[108] = {0}, digest[32];
    nvs_handle_t handle;
    esp_err_t error = nvs_flash_init_partition("falafel_data");
    if (error != ESP_OK) return false;
    error = nvs_open_from_partition("falafel_data", "falafel_id", NVS_READONLY, &handle);
    if (error != ESP_OK) return false;
    size_t size = sizeof(record);
    error = nvs_get_blob(handle, "identity", record, &size); nvs_close(handle);
    uint32_t own = 0, peer = 0;
    memcpy(&own, record + 4, 4); memcpy(&peer, record + 8, 4);
    bool valid = error == ESP_OK && size == sizeof(record) && !memcmp(record, "FCI1", 4) &&
        ntohl(own) == CONFIG_FALAFEL_DEVICE_ID && ntohl(peer) == CONFIG_FALAFEL_PEER_ID &&
        !falafel_crypto_sha256(record, 76, digest) &&
        falafel_crypto_constant_time_equal(digest, record + 76, 32);
    if (valid) memcpy(credential, record + 12, 64);
    falafel_crypto_secure_zero(record, sizeof(record));
    return valid;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_READY);
        ESP_LOGW(TAG, "Wi-Fi disconnected; retry scheduled");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_READY);
        ESP_LOGI(TAG, "Wi-Fi obtained IPv4");
    }
}

static bool config_valid(void)
{
    struct in_addr ip;
    size_t ssid = strlen(CONFIG_FALAFEL_WIFI_SSID);
    size_t password = strlen(CONFIG_FALAFEL_WIFI_PASSWORD);
    if (!ssid || ssid > 32 || password < 8 || password > 63 ||
        CONFIG_FALAFEL_DEVICE_ID == CONFIG_FALAFEL_PEER_ID ||
        inet_pton(AF_INET, CONFIG_FALAFEL_RELAY_IPV4, &ip) != 1) return false;
    uint32_t host = ntohl(ip.s_addr);
    /* This preparation intentionally targets private LAN relays only. */
    return (host >> 24) == 10 || (host >> 20) == 0xac1 || (host >> 16) == 0xc0a8;
}

static void network_worker(void *arg)
{
    (void)arg;
    int fd = -1;
    falafel_lab *lab = NULL;
    falafel_storage_espidf *store = NULL;
    falafel_relay_io io;
    falafel_relay_io_init(&io, -1, falafel_lab_receive, NULL);
    bool store_open = false;
    uint64_t report_after = 0;
    for (;;) {
        if (lab && !lab->mailbox.ready) break;
        if (!(xEventGroupGetBits(wifi_events) & WIFI_READY)) {
            if (fd >= 0) { close(fd); fd = -1; }
            esp_err_t error = esp_wifi_connect();
            if (error != ESP_OK && error != ESP_ERR_WIFI_CONN)
                ESP_LOGW(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(error));
            xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
            continue;
        }
        if (!lab) {
            /* Initialize/reseed only in the Wi-Fi runtime, never by enabling
             * the RF-disabled diagnostic's exclusive SAR source. */
            lab = calloc(1, sizeof(*lab)); store = calloc(1, sizeof(*store));
            if (!lab || !store) break;
            uint8_t credential[64] = {0};
            if (!load_credential(credential)) {
                ESP_LOGE(TAG, "Missing/invalid lab identity or routing IDs; explicit provisioning required");
                falafel_crypto_secure_zero(credential, sizeof(credential)); break;
            }
            esp_err_t error = falafel_storage_espidf_open(store, "falafel_data",
#ifdef CONFIG_FALAFEL_LAB_CREATE_MAILBOX
                                                        true);
#else
                                                        false);
#endif
            store_open = error == ESP_OK;
            falafel_mail_result result = FALAFEL_MAIL_IO;
            if (store_open) result = falafel_lab_open(lab, falafel_storage_espidf_mailbox(store),
                credential, CONFIG_FALAFEL_DEVICE_ID, CONFIG_FALAFEL_PEER_ID,
#ifdef CONFIG_FALAFEL_LAB_CREATE_MAILBOX
                true,
#else
                false,
#endif
                CONFIG_FALAFEL_PRESET_TEXT, falafel_relay_io_send, &io, lab_event, NULL);
            falafel_crypto_secure_zero(credential, sizeof(credential));
            if (result != FALAFEL_MAIL_OK) {
                ESP_LOGE(TAG, "Lab mailbox initialization failed code=%u storage=%s; no reset",
                         (unsigned)result, esp_err_to_name(error)); break;
            }
            resources();
        }
        if (fd < 0) {
            fd = falafel_relay_tcp_open(CONFIG_FALAFEL_RELAY_IPV4, CONFIG_FALAFEL_RELAY_PORT,
                                       CONFIG_FALAFEL_DEVICE_ID, CONFIG_FALAFEL_PEER_ID);
            if (fd < 0) {
                ESP_LOGW(TAG, "Relay TCP unavailable (errno=%d); retry in 2s", errno);
                vTaskDelay(pdMS_TO_TICKS(2000)); continue;
            }
            falafel_relay_io_init(&io, fd, falafel_lab_receive, lab);
            falafel_mail_link_connected(&lab->link);
            ESP_LOGI(TAG, "TCP connected; HELLO2 written own=%u peer=%u; awaiting E2EE receipt",
                     (unsigned)CONFIG_FALAFEL_DEVICE_ID, (unsigned)CONFIG_FALAFEL_PEER_ID);
        }
        uint64_t now = (uint64_t)(esp_timer_get_time() / 1000);
        if (!falafel_relay_io_poll(&io, now)) {
            close(fd); fd = -1;
            io.fd = -1;
            ESP_LOGW(TAG, "Relay TCP closed; registration will be retried");
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            falafel_mail_link_tick(&lab->link, now);
            if (now >= report_after) { resources(); report_after = now + 30000; }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (fd >= 0) close(fd);
    if (store_open) falafel_storage_espidf_close(store);
    if (lab) { falafel_lab_close(lab); free(lab); }
    free(store);
    ESP_LOGE(TAG, "Lab worker stopped; inspect identity/storage, never automatically erased");
    resources();
    vTaskDelete(NULL);
}

void falafel_relay_network_start(void)
{
    if (!config_valid()) {
        ESP_LOGE(TAG, "Invalid local network configuration (WPA2 password, IDs or private relay IPv4)");
        return;
    }
    /* Standard Wi-Fi NVS is distinct from falafel_data. Never auto-erase. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, CONFIG_FALAFEL_WIFI_SSID, strlen(CONFIG_FALAFEL_WIFI_SSID));
    memcpy(config.sta.password, CONFIG_FALAFEL_WIFI_PASSWORD, strlen(CONFIG_FALAFEL_WIFI_PASSWORD));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    memset(&config, 0, sizeof(config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(xTaskCreate(network_worker, "relay_network", 8192, NULL, 5, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
#else
void falafel_relay_network_start(void)
{ ESP_LOGI(TAG, "Network disabled; enable explicitly in local menuconfig"); }
#endif
