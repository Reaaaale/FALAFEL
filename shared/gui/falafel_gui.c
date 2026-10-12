#include <string.h>
#include "falafel_gui_internal.h"

falafel_gui_state_t falafel_gui = { .personality = FALAFEL_PERSONALITY_TAXI, .battery_percent = 100 };

static void show_only(lv_obj_t *visible)
{
    lv_obj_add_flag(falafel_gui.home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(falafel_gui.messages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(falafel_gui.composer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(visible, LV_OBJ_FLAG_HIDDEN);
}
void falafel_gui_show_home(void) { show_only(falafel_gui.home); }
void falafel_gui_show_messages(void) { falafel_gui.unread_count = 0; falafel_home_refresh(); show_only(falafel_gui.messages); }
void falafel_gui_show_composer(void) { falafel_composer_reset(); show_only(falafel_gui.composer); }
bool falafel_gui_is_home_active(void)
{
    return falafel_gui.home != NULL && !lv_obj_has_flag(falafel_gui.home, LV_OBJ_FLAG_HIDDEN);
}
void falafel_gui_set_callbacks(const falafel_gui_callbacks_t *callbacks)
{
    if (callbacks == NULL) memset(&falafel_gui.callbacks, 0, sizeof(falafel_gui.callbacks));
    else falafel_gui.callbacks = *callbacks;
}
static void notification_timeout(lv_timer_t *timer)
{
    lv_obj_add_flag(falafel_gui.notification, LV_OBJ_FLAG_HIDDEN); lv_timer_pause(timer);
}

void falafel_gui_create(void)
{
    falafel_gui.screen = lv_screen_active(); falafel_theme_apply_screen(falafel_gui.screen);
    lv_obj_clear_flag(falafel_gui.screen, LV_OBJ_FLAG_SCROLLABLE);
    falafel_gui.content = lv_obj_create(falafel_gui.screen); lv_obj_set_size(falafel_gui.content, 320, 480);
    lv_obj_align(falafel_gui.content, LV_ALIGN_CENTER, 0, 0); lv_obj_set_style_bg_opa(falafel_gui.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(falafel_gui.content, 0, 0); lv_obj_set_style_pad_all(falafel_gui.content, 0, 0);
    lv_obj_clear_flag(falafel_gui.content, LV_OBJ_FLAG_SCROLLABLE);
    falafel_gui.home = falafel_home_create(falafel_gui.content);
    falafel_gui.messages = falafel_messages_create(falafel_gui.content);
    falafel_gui.composer = falafel_composer_create(falafel_gui.content);
    falafel_gui.status_bar = falafel_status_bar_create(falafel_gui.content);
    falafel_gui.notification = lv_obj_create(falafel_gui.screen); lv_obj_set_size(falafel_gui.notification, 280, 52);
    lv_obj_align(falafel_gui.notification, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(falafel_gui.notification, lv_color_hex(FALAFEL_COLOR_NIGHT), 0);
    lv_obj_set_style_border_color(falafel_gui.notification, lv_color_hex(FALAFEL_COLOR_YELLOW), 0);
    lv_obj_set_style_border_width(falafel_gui.notification, 2, 0); lv_obj_set_style_radius(falafel_gui.notification, 8, 0);
    lv_obj_clear_flag(falafel_gui.notification, LV_OBJ_FLAG_SCROLLABLE);
    falafel_gui.notification_label = falafel_theme_create_label(falafel_gui.notification, "", &lv_font_montserrat_14, FALAFEL_COLOR_WHITE);
    lv_obj_center(falafel_gui.notification_label); lv_obj_add_flag(falafel_gui.notification, LV_OBJ_FLAG_HIDDEN);
    falafel_gui.notification_timer = lv_timer_create(notification_timeout, 2500, NULL);
    lv_timer_pause(falafel_gui.notification_timer); falafel_gui_show_home();
}

void falafel_gui_set_wifi_connected(bool connected) { falafel_gui.wifi_connected = connected; if (falafel_gui.status_bar) falafel_status_bar_refresh(); }
void falafel_gui_set_peer_connected(bool connected) { falafel_gui.peer_connected = connected; if (falafel_gui.status_bar) falafel_status_bar_refresh(); }
void falafel_gui_set_battery_percent(uint8_t percent) { falafel_gui.battery_percent = percent > 100 ? 100 : percent; if (falafel_gui.status_bar) falafel_status_bar_refresh(); }
void falafel_gui_set_unread_count(uint16_t count) { falafel_gui.unread_count = count; if (falafel_gui.home) falafel_home_refresh(); }
void falafel_gui_show_notification(const char *text)
{
    if (!falafel_gui.notification || !text) return;
    lv_label_set_text(falafel_gui.notification_label, text); lv_obj_clear_flag(falafel_gui.notification, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(falafel_gui.notification); lv_timer_reset(falafel_gui.notification_timer); lv_timer_resume(falafel_gui.notification_timer);
}
void falafel_gui_add_received_message(const char *text) { falafel_messages_add(text, false); }
void falafel_gui_add_sent_message(const char *text) { falafel_messages_add(text, true); }
