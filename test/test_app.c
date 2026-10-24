#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "falafel_app.h"

static bool fake_next_message_id(void *user_data, uint64_t *message_id)
{
    (void)user_data;
    *message_id = UINT64_C(42);
    return true;
}

static bool fake_current_time(void *user_data, uint64_t *timestamp)
{
    (void)user_data;
    *timestamp = UINT64_C(1700000000);
    return true;
}

static void test_app_init(void)
{
    int user_data = 123;
    const falafel_app_config_t config = {
        .next_message_id = fake_next_message_id,
        .current_time = fake_current_time,
        .user_data = &user_data,
    };
    falafel_app_t app;

    assert(falafel_app_init(&app, &config) == FALAFEL_APP_OK);
    assert(app.config.next_message_id == fake_next_message_id);
    assert(app.config.current_time == fake_current_time);
    assert(app.config.user_data == &user_data);
}

static void test_app_init_rejects_invalid_arguments(void)
{
    const falafel_app_config_t valid_config = {
        .next_message_id = fake_next_message_id,
        .current_time = fake_current_time,
        .user_data = NULL,
    };
    const falafel_app_config_t missing_id_provider = {
        .next_message_id = NULL,
        .current_time = fake_current_time,
        .user_data = NULL,
    };
    const falafel_app_config_t missing_time_provider = {
        .next_message_id = fake_next_message_id,
        .current_time = NULL,
        .user_data = NULL,
    };
    falafel_app_t app;

    assert(falafel_app_init(&app, &valid_config) == FALAFEL_APP_OK);

    assert(
        falafel_app_init(NULL, &valid_config) ==
        FALAFEL_APP_INVALID_ARGUMENT
    );
    assert(
        falafel_app_init(&app, NULL) ==
        FALAFEL_APP_INVALID_ARGUMENT
    );
    assert(
        falafel_app_init(&app, &missing_id_provider) ==
        FALAFEL_APP_INVALID_ARGUMENT
    );
    assert(
        falafel_app_init(&app, &missing_time_provider) ==
        FALAFEL_APP_INVALID_ARGUMENT
    );

    assert(app.config.next_message_id == fake_next_message_id);
    assert(app.config.current_time == fake_current_time);
    assert(app.config.user_data == NULL);
}

int main(void)
{
    test_app_init();
    test_app_init_rejects_invalid_arguments();

    puts("All application initialization tests passed.");
    return 0;
}
