#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "falafel_app.h"

typedef struct {
    uint64_t id;
    uint64_t time;
    bool fail_id;
    bool fail_time;
    bool send_ok;
    size_t sends;
} fixture_t;

static bool provide_id(void *data, uint64_t *value)
{
    fixture_t *fixture = data;
    if (fixture->fail_id) return false;
    *value = ++fixture->id;
    return true;
}

static bool provide_time(void *data, uint64_t *value)
{
    fixture_t *fixture = data;
    if (fixture->fail_time) return false;
    *value = ++fixture->time;
    return true;
}

static bool send_event(void *data, const falafel_event_t *event)
{
    fixture_t *fixture = data;
    (void)event;
    ++fixture->sends;
    return fixture->send_ok;
}

static falafel_app_t make_app(fixture_t *fixture, uint32_t own, uint32_t peer)
{
    falafel_app_t app;
    falafel_app_config_t config = {
        provide_id, provide_time, send_event, fixture, own, peer
    };
    assert(falafel_app_init(&app, &config) == FALAFEL_APP_OK);
    return app;
}

static falafel_event_t make_personality(uint32_t source, uint32_t destination,
                                        uint64_t id, uint64_t revision,
                                        uint8_t personality)
{
    falafel_event_t event = {
        .source_id = source,
        .destination_id = destination,
        .id = id,
        .timestamp = 1,
        .revision = revision,
        .type = FALAFEL_EVENT_PERSONALITY,
        .personality = personality,
    };
    return event;
}

static void test_creation_and_queue(void)
{
    fixture_t fixture = {.send_ok = true};
    falafel_app_t app = make_app(&fixture, 1, 2);
    uint64_t id = 0;
    char long_text[FALAFEL_MAX_PAYLOAD_SIZE + 2U];

    assert(falafel_app_create_text(&app, "ciao", &id) == FALAFEL_APP_OK);
    assert(id == 1U && app.queue_count == 1U);
    assert(falafel_app_create_text(&app, "", NULL) == FALAFEL_APP_EMPTY_TEXT);
    memset(long_text, 'x', sizeof(long_text));
    long_text[sizeof(long_text) - 1U] = '\0';
    assert(falafel_app_create_text(&app, long_text, NULL) ==
           FALAFEL_APP_TEXT_TOO_LONG);

    size_t before = app.queue_count;
    fixture.fail_id = true;
    assert(falafel_app_create_text(&app, "x", NULL) ==
           FALAFEL_APP_PROVIDER_FAILED);
    assert(app.queue_count == before);
    fixture.fail_id = false;
    fixture.fail_time = true;
    assert(falafel_app_create_text(&app, "x", NULL) ==
           FALAFEL_APP_PROVIDER_FAILED);
    assert(app.queue_count == before);
}

static void test_acknowledgements_and_retry(void)
{
    fixture_t fixture = {.send_ok = true};
    falafel_app_t app = make_app(&fixture, 1, 2);
    assert(falafel_app_create_text(&app, "retry", NULL) == FALAFEL_APP_OK);
    uint64_t id = app.queue[0].event.id;

    assert(falafel_app_flush(&app) == FALAFEL_APP_OK);
    assert(fixture.sends == 1U);
    assert(falafel_app_transition(&app, id, FALAFEL_MESSAGE_RELAY_ACCEPTED) ==
           FALAFEL_APP_OK);
    assert(falafel_app_flush(&app) == FALAFEL_APP_OK);
    assert(fixture.sends == 2U);
    assert(falafel_app_transition(&app, id, FALAFEL_MESSAGE_RELAY_ACCEPTED) ==
           FALAFEL_APP_OK);
    assert(falafel_app_transition(&app, id,
                                  FALAFEL_MESSAGE_DEVICE_RECEIVED) ==
           FALAFEL_APP_OK);
    assert(falafel_app_transition(&app, id,
                                  FALAFEL_MESSAGE_DEVICE_RECEIVED) ==
           FALAFEL_APP_OK);
}

static void test_origin_validation(void)
{
    fixture_t fixture = {.send_ok = true};
    falafel_app_t app = make_app(&fixture, 1, 2);
    assert(falafel_app_create_text(&app, "x", NULL) == FALAFEL_APP_OK);
    uint64_t id = app.queue[0].event.id;
    falafel_event_t acknowledgement = {
        .source_id = 2,
        .destination_id = 1,
        .id = id,
        .timestamp = 1,
        .type = FALAFEL_EVENT_ACK,
        .ack_phase = FALAFEL_ACK_RELAY_ACCEPTED,
    };
    assert(falafel_app_receive(&app, &acknowledgement, NULL) ==
           FALAFEL_APP_INVALID_ARGUMENT);
    acknowledgement.source_id = FALAFEL_RELAY_ID;
    assert(falafel_app_receive(&app, &acknowledgement, NULL) == FALAFEL_APP_OK);
    acknowledgement.source_id = 77;
    acknowledgement.ack_phase = FALAFEL_ACK_DEVICE_RECEIVED;
    assert(falafel_app_receive(&app, &acknowledgement, NULL) ==
           FALAFEL_APP_INVALID_ARGUMENT);
    acknowledgement.source_id = FALAFEL_RELAY_ID;
    assert(falafel_app_receive(&app, &acknowledgement, NULL) == FALAFEL_APP_OK);

    falafel_event_t text = {
        .source_id = 77, .destination_id = 1, .id = 99, .timestamp = 1,
        .type = FALAFEL_EVENT_TEXT, .payload_length = 1, .payload = {'x'}
    };
    assert(falafel_app_receive(&app, &text, NULL) ==
           FALAFEL_APP_INVALID_ARGUMENT);
    text.source_id = 2;
    text.destination_id = 77;
    assert(falafel_app_receive(&app, &text, NULL) ==
           FALAFEL_APP_INVALID_ARGUMENT);
}

static void test_personality_coalescing_and_conflict(void)
{
    fixture_t first_fixture = {.send_ok = true};
    fixture_t second_fixture = {.send_ok = true};
    falafel_app_t first = make_app(&first_fixture, 1, 2);
    falafel_app_t second = make_app(&second_fixture, 2, 1);
    bool changed = false;

    assert(falafel_app_set_personality(&first, 1) == FALAFEL_APP_OK);
    assert(falafel_app_set_personality(&first, 2) == FALAFEL_APP_OK);
    assert(first.queue_count == 1U);
    assert(first.queue[0].event.personality == 2U);
    assert(first.queue[0].event.revision == 2U);
    assert(falafel_app_flush(&first) == FALAFEL_APP_OK);
    assert(falafel_app_set_personality(&first, 1) == FALAFEL_APP_OK);
    assert(first.queue_count == 2U);

    assert(falafel_app_set_personality(&second, 0) == FALAFEL_APP_OK);
    falafel_event_t from_first = first.queue[1].event;
    falafel_event_t from_second = second.queue[0].event;
    assert(from_first.revision > from_second.revision);
    assert(falafel_app_receive(&second, &from_first, &changed) ==
           FALAFEL_APP_OK && changed);
    assert(falafel_app_receive(&first, &from_second, &changed) ==
           FALAFEL_APP_IGNORED);
    assert(first.personality == second.personality);

    falafel_event_t duplicate = from_first;
    assert(falafel_app_receive(&second, &duplicate, &changed) ==
           FALAFEL_APP_DUPLICATE);
    falafel_event_t old = make_personality(1, 2, 100, 1, 0);
    assert(falafel_app_receive(&second, &old, &changed) ==
           FALAFEL_APP_IGNORED);
}

static void test_full_queues(void)
{
    fixture_t fixture = {.send_ok = true};
    falafel_app_t app = make_app(&fixture, 1, 2);
    for (size_t index = 0; index < FALAFEL_APP_QUEUE_CAPACITY; ++index) {
        assert(falafel_app_create_text(&app, "x", NULL) == FALAFEL_APP_OK);
    }
    assert(falafel_app_create_text(&app, "full", NULL) ==
           FALAFEL_APP_QUEUE_FULL);
}

int main(void)
{
    test_creation_and_queue();
    test_acknowledgements_and_retry();
    test_origin_validation();
    test_personality_coalescing_and_conflict();
    test_full_queues();
    puts("All application tests passed.");
    return 0;
}
