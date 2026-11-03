#include "falafel_gui_internal.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *message_list;
static lv_obj_t *empty_label;
typedef struct { uint8_t id[16]; bool sent, read; lv_obj_t *row,*message,*label; } message_view;
static message_view views[1024];
static unsigned view_count;

bool falafel_gui_send_preset(void)
{
    if (!falafel_gui.callbacks.on_send_message ||
        !falafel_gui.callbacks.on_send_message("Sto arrivando", falafel_gui.callbacks.user_data)) {
        falafel_gui_show_notification("Invio non disponibile");
        return false;
    }
    return true;
}
static void preset_clicked(lv_event_t *event) { (void)event; falafel_gui_send_preset(); }

static void back_clicked(lv_event_t *event) { (void)event; falafel_gui_show_home(); }
static lv_obj_t *header_button(lv_obj_t *parent, const char *text, int32_t width, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 40);
    falafel_theme_style_button(button, color);
    lv_obj_t *label = falafel_theme_create_label(button, text, &lv_font_montserrat_14,
                                                  FALAFEL_COLOR_CREAM);
    lv_obj_center(label);
    return button;
}

lv_obj_t *falafel_messages_create(lv_obj_t *parent)
{
    view_count=0;
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 466, 466);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = falafel_theme_create_label(root, "Messaggi",
                                                 &lv_font_montserrat_22, FALAFEL_COLOR_WHITE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 76);

    message_list = lv_obj_create(root);
    lv_obj_set_size(message_list, 286, 158);
    lv_obj_align(message_list, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_bg_color(message_list, lv_color_hex(FALAFEL_COLOR_SAGE), 0);
    lv_obj_set_style_border_color(message_list, lv_color_hex(FALAFEL_COLOR_SAGE), 0);
    lv_obj_set_style_border_width(message_list, 2, 0);
    lv_obj_set_style_radius(message_list, 8, 0);
    lv_obj_set_style_pad_all(message_list, 10, 0);
    lv_obj_set_style_pad_row(message_list, 8, 0);
    lv_obj_set_flex_flow(message_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(message_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(message_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(message_list, lv_color_hex(FALAFEL_COLOR_HORN_BLUE), LV_PART_SCROLLBAR);

    empty_label = falafel_theme_create_label(message_list, "Nessun messaggio disponibile",
        &lv_font_montserrat_12, FALAFEL_COLOR_CREAM);
    lv_obj_t *preset = header_button(root, "Invia: Sto arrivando", 220, FALAFEL_COLOR_SURFACE);
    lv_obj_align(preset, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_add_event_cb(preset, preset_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = header_button(root, "Torna a Falafel", 202, FALAFEL_COLOR_YELLOW);
    lv_obj_set_height(back, 48);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 330);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scope = falafel_theme_create_label(root,
        "Storico locale cifrato.\nLettura su questo dispositivo.", &lv_font_montserrat_12, FALAFEL_COLOR_CREAM);
    lv_obj_set_style_text_align(scope, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(scope, LV_ALIGN_TOP_MID, 0, 388);
    return root;
}

static void add_row(const char *text, bool sent, message_view *view)
{
    if (message_list == NULL || text == NULL || text[0] == '\0') return;
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *row = lv_obj_create(message_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *message = lv_obj_create(row);
    lv_obj_set_width(message, 220);
    lv_obj_set_height(message, LV_SIZE_CONTENT);
    lv_obj_align(message, sent ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(message, lv_color_hex(
        sent ? FALAFEL_COLOR_HORN_BLUE : FALAFEL_COLOR_PALE), 0);
    lv_obj_set_style_border_color(message, lv_color_hex(FALAFEL_COLOR_SAGE), 0);
    lv_obj_set_style_border_width(message, 2, 0);
    lv_obj_set_style_radius(message, 6, 0);
    lv_obj_set_style_pad_all(message, 11, 0);
    lv_obj_clear_flag(message, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = falafel_theme_create_label(message, text, &lv_font_montserrat_14,
                                                  FALAFEL_COLOR_CREAM);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 194);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_update_layout(label);
    int32_t height=lv_obj_get_height(label)+22;
    lv_obj_set_height(message,height);
    lv_obj_set_height(row,height);
    if(view) { view->row=row; view->message=message; view->label=label; }
    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

void falafel_messages_add(const char *text, bool sent) { add_row(text,sent,NULL); }
void falafel_gui_message_update(const uint8_t id[16], bool sent, const char *text, uint8_t delivery, bool read)
{
    message_view *view=NULL;
    for(unsigned i=0;i<view_count;++i) if(views[i].sent==sent && !memcmp(views[i].id,id,16)) { view=&views[i]; break; }
    if(!view) {
        if(view_count==1024) return;
        view=&views[view_count++]; memset(view,0,sizeof(*view)); memcpy(view->id,id,16); view->sent=sent;
        add_row(text,sent,view);
    }
    view->read=read;
    const char *status=sent?(delivery==2?"Accettato dal destinatario":delivery==1?"In invio":"In attesa"):
        read?"Letto su questo dispositivo":"Da leggere";
    char display[220]; snprintf(display,sizeof(display),"%s\n%s",text,status);
    lv_label_set_text(view->label,display); lv_obj_update_layout(view->label);
    int32_t height=lv_obj_get_height(view->label)+22;
    lv_obj_set_height(view->message,height);
    lv_obj_set_height(view->row,height);
}
unsigned falafel_gui_message_count(void) { return view_count; }
unsigned falafel_gui_message_cache_bytes(void) { return (unsigned)sizeof(views); }
unsigned falafel_gui_message_unread_count(void)
{
    unsigned count=0; for(unsigned i=0;i<view_count;++i) if(!views[i].sent && !views[i].read) ++count;
    return count;
}
