#include "falafel_app.h"

falafel_app_result_t falafel_app_init(
    falafel_app_t *app,
    const falafel_app_config_t *config
)
{
    if (app == NULL || config == NULL) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }

    if (config->next_message_id == NULL ||
        config->current_time == NULL) {
        return FALAFEL_APP_INVALID_ARGUMENT;
    }

    app->config = *config;

    return FALAFEL_APP_OK;
}