#include "falafel_crypto.h"
#include "sec003_suite.h"
#include "sec003_probe.h"
#include "bootloader_random.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/build_info.h"
#include <inttypes.h>
#include <stdio.h>

#if !CONFIG_IDF_TARGET_ESP32C6 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 6)
#error "SEC-003 requires ESP32-C6 and the pinned ESP-IDF 5.5.6 baseline"
#endif
#if MBEDTLS_VERSION_NUMBER != 0x03060700 || !defined(MBEDTLS_ENTROPY_HARDWARE_ALT) || \
    !defined(MBEDTLS_NO_PLATFORM_ENTROPY) || !defined(MBEDTLS_AES_ALT) || !defined(MBEDTLS_GCM_ALT)
#error "SEC-003 crypto/entropy configuration differs from the reviewed C6 baseline"
#endif
#if defined(CONFIG_SECURE_BOOT) || defined(CONFIG_SECURE_FLASH_ENC_ENABLED) || \
    defined(CONFIG_PM_ENABLE) || !defined(CONFIG_ESP_COREDUMP_ENABLE_TO_NONE) || \
    !defined(CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT) || !defined(CONFIG_ESPTOOLPY_AFTER_NORESET)
#error "Use the SEC-003 safety overlay; no security provisioning or key-bearing panic dumps"
#endif

static const char *TAG = "falafel_crypto_test";
static void report(void *user, const char *name, bool ok)
{
    (void)user;
    ESP_LOGI(TAG, "SEC003 CASE %s %s", name, ok ? "PASS" : "FAIL");
}
static void yield_test(void *user)
{
    (void)user;
    vTaskDelay(1); /* Let idle/watchdog run; power management is disabled. */
}
static void public_hex(const uint8_t bytes[32], char out[65])
{
    for (unsigned i = 0; i < 32; ++i) (void)snprintf(out + 2 * i, 3, "%02x", bytes[i]);
}

void app_main(void)
{
    /* Allows USB-UART reconnection/capture after a genuine power cycle.
     * No crypto, identity generation or entropy ownership during this delay. */
    ESP_LOGI(TAG, "SEC003 WAIT start_delay_ms=10000");
    vTaskDelay(pdMS_TO_TICKS(10000));
    char elf_hex[65], fingerprint[65];
    public_hex(esp_app_get_description()->app_elf_sha256, elf_hex);
    ESP_LOGI(TAG, "SEC003 BEGIN platform=ESP32C6 idf=%s mbed=%s elf_sha256=%s reset_reason=%d",
             esp_get_idf_version(), MBEDTLS_VERSION_STRING, elf_hex, (int)esp_reset_reason());
    ESP_LOGW(TAG, "SEC003 POLICY SAR_EXCLUSIVE no_RF_or_user_ADC no_sleep test_only=1");
    ESP_LOGW(TAG, "SEC003 Permanent identities NO_GO; statistics and counters do not prove entropy");
    int64_t started = esp_timer_get_time();
    size_t heap_before = esp_get_free_heap_size();
    sec003_result result = {0};
    bool ok = false;
    bool entropy_owned = false;
    falafel_crypto_context_t *context = NULL;
    bool resources_ok = heap_caps_check_integrity_all(false);
    if (!resources_ok) goto finished;
    context = heap_caps_calloc(1, sizeof(*context), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!context) {
        report(NULL, "allocation", false);
        result.failed = 1;
        goto finished;
    }
    /* Exclusive to this diagnostic app. No RF, user ADC or power-management
     * initialization occurs. Keep the source available for every reseed. */
    bootloader_random_enable();
    entropy_owned = true;
    ESP_LOGI(TAG, "SEC003 SOURCE enable_requested=1 physical_health=NOT_MEASURED");
    sec003_hooks callbacks = {.case_result = report, .yield = yield_test};
    ok = sec003_suite_run(context, &callbacks, &result);

finished:
    /* Suite frees the provider and wipes all its key/scratch buffers on errors
     * as well as success. The outer owner then releases memory and SAR ownership. */
    if (context) {
        falafel_crypto_secure_zero(context, sizeof(*context));
        heap_caps_free(context);
    }
    if (entropy_owned) bootloader_random_disable();
    resources_ok = resources_ok && heap_caps_check_integrity_all(false) &&
                   uxTaskGetStackHighWaterMark(NULL) >= 512 && result.cleaned;
    report(NULL, "platform_resources", resources_ok);
    if (resources_ok) ++result.passed; else ++result.failed;
    ok = ok && resources_ok;
    sec003_probe_stats stats = sec003_probe_get();
    ESP_LOGI(TAG, "SEC003 PROBE entropy_calls=%" PRIu32 " injected_failures=%" PRIu32
             " hardware_calls=%" PRIu32 " hardware_bytes=%" PRIu32,
             stats.entropy_calls, stats.injected_failures, stats.hardware_calls, stats.hardware_bytes);
    ESP_LOGI(TAG, "SEC003 RESOURCES heap_before=%u heap_after=%u largest_block=%u stack_min_bytes=%u elapsed_us=%lld",
             (unsigned)heap_before, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (long long)(esp_timer_get_time() - started));
    if (ok) {
        /* Only a PUBLIC ephemeral-key digest for cross-boot repeat detection.
         * Never print random samples, private keys, DH secrets or derived keys. */
        public_hex(result.public_boot_fingerprint, fingerprint);
        ESP_LOGI(TAG, "SEC003 PUBLIC_BOOT_FINGERPRINT %s", fingerprint);
    }
    ESP_LOGI(TAG, "SEC003 RESULT %s passed=%u failed=%u cleanup=%u",
             ok ? "PASS" : "FAIL", result.passed, result.failed, result.cleaned ? 1u : 0u);
    ESP_LOGW(TAG, "SEC003 STATUS hardware_review=PENDING permanent_identities=NO_GO");
}
