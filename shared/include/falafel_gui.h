#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FALAFEL_PERSONALITY_TAXI = 0,
    FALAFEL_PERSONALITY_COOK,
    FALAFEL_PERSONALITY_REMINDER,
    FALAFEL_PERSONALITY_COUNT
} falafel_personality_t;

typedef struct {
    void (*on_send_message)(const char *text, void *user_data);
    void (*on_personality_changed)(falafel_personality_t personality,
                                   void *user_data);
    void (*on_messages_opened)(void *user_data);
    void *user_data;
} falafel_gui_callbacks_t;

/** Configure application callbacks. The pointed-to structure is copied. */
void falafel_gui_set_callbacks(const falafel_gui_callbacks_t *callbacks);

/** Create the GUI on the active LVGL screen. Call once after lv_init(). */
void falafel_gui_create(void);

void falafel_gui_set_wifi_connected(bool connected);
void falafel_gui_set_peer_connected(bool connected);
void falafel_gui_set_battery_percent(uint8_t percent);
void falafel_gui_set_unread_count(uint16_t count);

/** Select the adjacent personality. Return true only when it changed. */
bool falafel_gui_next_personality(void);
bool falafel_gui_previous_personality(void);

/** True while the home/personality screen is the active screen. */
bool falafel_gui_is_home_active(void);

/** Show a non-blocking overlay for a short time. */
void falafel_gui_show_notification(const char *text);

/** Append messages to the scrollable conversation. Text is copied by LVGL. */
void falafel_gui_add_received_message(const char *text);
void falafel_gui_add_sent_message(const char *text);

#ifdef __cplusplus
}
#endif
