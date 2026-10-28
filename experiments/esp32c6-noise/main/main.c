#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

int noise_feasibility_run(void);

void app_main(void)
{
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    int64_t started = esp_timer_get_time();
    int result = noise_feasibility_run();
    int64_t elapsed = esp_timer_get_time() - started;
    size_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    printf("esp32c6_noise_result: %s\n", result == 0 ? "PASS" : "FAIL");
    printf("elapsed_us: %lld\n", (long long)elapsed);
    printf("heap_before: %zu\nheap_after: %zu\n", before, after);
    printf("largest_before: %zu\nlargest_after: %zu\n", largest_before, largest_after);
    printf("minimum_free_stack_bytes: %u\n",
           (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
}
