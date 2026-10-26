#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "desktop_transport.h"

static bool provide_id(void *data, uint64_t *value)
{
    (void)data;
    *value = 7;
    return true;
}

static bool provide_time(void *data, uint64_t *value)
{
    (void)data;
    *value = 9;
    return true;
}

static falafel_app_t make_app(void)
{
    falafel_app_t app;
    falafel_app_config_t config = {
        provide_id, provide_time, NULL, NULL, 1, 2
    };
    assert(falafel_app_init(&app, &config) == FALAFEL_APP_OK);
    return app;
}

static void write_bytes(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *stream = fopen(path, "wb");
    assert(stream != NULL);
    assert(fwrite(bytes, 1U, size, stream) == size);
    assert(fclose(stream) == 0);
}

int main(void)
{
    const char *path = "/tmp/falafel-state-test.bin";
    falafel_app_t original = make_app();
    falafel_app_t loaded = make_app();
    uint32_t reserved = 0;

    assert(falafel_app_create_text(&original, "persist", NULL) ==
           FALAFEL_APP_OK);
    original.personality = 2;
    original.personality_revision = 4;
    original.personality_author = 2;
    original.seen_count = 1;
    original.seen_ids[0] = 55;
    original.seen_sources[0] = 2;
    assert(desktop_state_save(path, &original, 128));
    assert(desktop_state_load(path, &loaded, &reserved) == DESKTOP_STATE_OK);
    assert(reserved == 128U);
    assert(loaded.personality == 2U && loaded.personality_revision == 4U);
    assert(loaded.queue_count == 1U && loaded.seen_ids[0] == 55U);

    falafel_app_t empty = make_app();
    uint32_t next = 0;
    uint32_t high_watermark = 0;
    uint64_t first_id;
    assert(desktop_allocate_event_id(path, &empty, 1, &next,
                                     &high_watermark, &first_id));
    assert(first_id == (UINT64_C(1) << 32 | UINT64_C(1)));
    assert(high_watermark == 64U);
    assert(desktop_state_save(path, &empty, high_watermark));
    uint32_t loaded_high_watermark = 0;
    loaded = make_app();
    assert(desktop_state_load(path, &loaded, &loaded_high_watermark) ==
           DESKTOP_STATE_OK);
    next = loaded_high_watermark;
    uint64_t after_restart;
    assert(desktop_allocate_event_id(path, &loaded, 1, &next,
                                     &loaded_high_watermark, &after_restart));
    assert(after_restart > first_id);
    assert((uint32_t)after_restart == 65U);

    FILE *stream = fopen(path, "r+b");
    assert(stream != NULL);
    assert(fseek(stream, 20, SEEK_SET) == 0);
    assert(fputc(0xA5, stream) != EOF);
    assert(fclose(stream) == 0);
    assert(desktop_state_load(path, &loaded, &reserved) ==
           DESKTOP_STATE_CORRUPT);

    const uint8_t unsupported[] = {
        'F','A','L','S','T','V','2','\0', 0, 99, 0, 0, 0, 0, 0, 0, 0, 0
    };
    write_bytes(path, unsupported, sizeof(unsupported));
    assert(desktop_state_load(path, &loaded, &reserved) ==
           DESKTOP_STATE_UNSUPPORTED);
    remove(path);
    puts("Desktop persistence tests passed.");
    return 0;
}
