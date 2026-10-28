#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
void noise_rand_bytes(void *bytes, size_t size) { esp_fill_random(bytes, size); }
#else
#include <stdlib.h>
#include <sys/random.h>
void noise_rand_bytes(void *bytes, size_t size)
{
    unsigned char *out = bytes;
    while (size != 0u) {
        size_t count = size > 256u ? 256u : size;
        if (getentropy(out, count) != 0) abort();
        out += count;
        size -= count;
    }
}
#endif
