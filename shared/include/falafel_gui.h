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

/** Navigation is GUI-local; it never changes application/transport state. */
typedef enum {
    FALAFEL_PAGE_HOME = 0,
    FALAFEL_PAGE_MENU,
    FALAFEL_PAGE_MESSAGES,
    FALAFEL_PAGE_GAMES,
    FALAFEL_PAGE_SETTINGS,
    FALAFEL_PAGE_COMPOSER,
    FALAFEL_PAGE_COUNT
} falafel_gui_page_t;

/** One debounced physical press. Call on the LVGL task after GUI creation,
 * never from a GPIO ISR. Opens Menu from any page; repeated presses are inert. */
void falafel_gui_menu_button_pressed(void);
void falafel_gui_open_menu(void);
void falafel_gui_navigate(falafel_gui_page_t page);
void falafel_gui_go_back(void);
falafel_gui_page_t falafel_gui_current_page(void);

typedef struct {
    /** True accepts a volatile send request only; persistence is asynchronous. */
    bool (*on_send_message)(const char *text, void *user_data);
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

/** Start an adjacent-personality rotation; false at limits or while busy.
 * Next traverses the frame cycle backwards; previous advances it.
 * Selection/callback commit after a full turn; the displayed pose is retained. */
bool falafel_gui_next_personality(void);
bool falafel_gui_previous_personality(void);
/** Input adapters can discard a gesture that begins during playback. */
bool falafel_gui_is_personality_animating(void);
/** Apply synchronized state without firing the local-change callback. */
bool falafel_gui_set_personality_remote(falafel_personality_t personality);

/** True while the home/personality screen is the active screen. */
bool falafel_gui_is_home_active(void);
bool falafel_gui_is_messages_active(void);
void falafel_gui_open_messages(void);
/** Preset action shared by the button and simulator integration automation. */
bool falafel_gui_send_preset(void);

/** Show a non-blocking overlay for a short time. */
void falafel_gui_show_notification(const char *text);

/** Append messages to the scrollable conversation. Text is copied by LVGL. */
void falafel_gui_add_received_message(const char *text);
void falafel_gui_add_sent_message(const char *text);

/** Idempotent projection of durable history. read is LOCAL, not a peer receipt. */
void falafel_gui_message_update(const uint8_t id[16], bool sent, const char *text, uint8_t delivery, bool read);
unsigned falafel_gui_message_count(void);
unsigned falafel_gui_message_unread_count(void);
unsigned falafel_gui_message_cache_bytes(void);

#ifdef __cplusplus
}
#endif
