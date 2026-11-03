#include "ui_smoke.h"
#include "falafel_gui_internal.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>

static void pointer(SDL_Window *w, Uint32 type, int x, int y)
{
    SDL_Event e = {0}; e.type = type;
    if(type == SDL_MOUSEMOTION) {
        e.motion.windowID = SDL_GetWindowID(w); e.motion.x=x; e.motion.y=y;
        e.motion.state=SDL_BUTTON_LMASK;
    } else {
        e.button.windowID=SDL_GetWindowID(w); e.button.button=SDL_BUTTON_LEFT;
        e.button.state=type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;
        e.button.x=x; e.button.y=y;
    }
    SDL_PushEvent(&e);
}
static void wheel(SDL_Window *w, float x)
{
    SDL_Event e={0}; e.type=SDL_MOUSEWHEEL; e.wheel.windowID=SDL_GetWindowID(w);
    e.wheel.timestamp=SDL_GetTicks(); e.wheel.preciseX=x; e.wheel.x=(int)x;
    e.wheel.direction=SDL_MOUSEWHEEL_NORMAL; SDL_PushEvent(&e);
}
static int capture(lv_display_t *d, const char *name)
{
    (void)d;
    const char *directory=getenv("FALAFEL_SCREENSHOTS");
    /* Snapshot the LVGL tree: SDL backbuffers are undefined after Present. */
    lv_obj_t *probe=lv_obj_create(falafel_gui.content);
    lv_obj_remove_style_all(probe); lv_obj_set_size(probe,30,30);
    lv_obj_set_style_bg_opa(probe,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(probe,lv_color_white(),0);
    lv_draw_buf_t *buf=lv_snapshot_take(lv_screen_active(),LV_COLOR_FORMAT_ARGB8888);
    lv_obj_delete(probe);
    if(!buf) return -1;
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormatFrom(buf->data,
        (int)buf->header.w,(int)buf->header.h,32,(int)buf->header.stride,SDL_PIXELFORMAT_ARGB8888);
    if(!surface) { lv_draw_buf_destroy(buf); return -1; }
    Uint32 *corner=(Uint32*)((Uint8*)surface->pixels+10*surface->pitch)+10;
    Uint32 *center=(Uint32*)((Uint8*)surface->pixels+233*surface->pitch)+233;
    int result=((*corner & 0xffffff)!=FALAFEL_COLOR_SAGE ||
                (name[0]=='h' && (*center & 0xffffff)==FALAFEL_COLOR_SAGE)) ? -1 : 0;
    if(directory && !result) {
        char path[1024]; snprintf(path,sizeof(path),"%s/%s.bmp",directory,name);
        result=SDL_SaveBMP(surface,path);
    }
    SDL_FreeSurface(surface); lv_draw_buf_destroy(buf); return result;
}
/* Observe real draws, not just timer advances. This catches skipped frames. */
static uint32_t drawn_frames, changes, heartbeat_count, heartbeat_gap, last_heartbeat;
static uint32_t first_frame_ms;
static bool invalid_frame_order;
static lv_timer_t *heartbeat_timer;
static falafel_gui_callbacks_t original_callbacks;

static lv_obj_t *character(void)
{
    return lv_obj_get_child(lv_obj_get_child(falafel_gui.home, 0), 0);
}

static void frame_drawn(lv_event_t *event)
{
    (void)event;
    if (!falafel_gui.personality_animating) return;
    if (!drawn_frames) first_frame_ms = SDL_GetTicks();
    uint32_t bit = 1u << falafel_gui.rotation_frame;
    if (!(drawn_frames & bit) && drawn_frames != bit - 1) invalid_frame_order = true;
    if (lv_image_get_src(character()) != &falafel_asset_rotation[falafel_gui.rotation_frame])
        invalid_frame_order = true;
    drawn_frames |= bit;
}

static void heartbeat(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = SDL_GetTicks();
    if (last_heartbeat && now - last_heartbeat > heartbeat_gap)
        heartbeat_gap = now - last_heartbeat;
    last_heartbeat = now;
    ++heartbeat_count;
}

static void personality_changed(falafel_personality_t selected, void *data)
{
    ++changes;
    printf("[UI] rotation completed after %u ms from first draw\n",
           SDL_GetTicks() - first_frame_ms);
    if (original_callbacks.on_personality_changed)
        original_callbacks.on_personality_changed(selected, data);
}

int ui_smoke_step(lv_display_t *d, SDL_Window *w)
{
    static unsigned phase; static Uint32 start, wait_started;
    static const Uint32 times[]={0,200,300,400,600,1400,1550,1650,1850,2400,
                                 2500,2700,2900,3800,4600,5500};
    const uint32_t all_frames = (1u << FALAFEL_ROTATION_FRAME_COUNT) - 1;
    if(!start) start=SDL_GetTicks();
    if(phase>=sizeof(times)/sizeof(times[0])) return 1;
    if(SDL_GetTicks()-start<times[phase]) return 0;
    int width,height; SDL_GetWindowSize(w,&width,&height);
    if(width!=466 || height!=466) goto failure;
    /* Instrumented decoders are slower. Wait for completion without assuming
     * desktop release timing, while retaining a bounded failure deadline. */
    if (phase==5 || phase==9 || phase==13) {
        if (falafel_gui.personality_animating) {
            if (!wait_started) wait_started=SDL_GetTicks();
            if (SDL_GetTicks()-wait_started>4000) goto failure;
            return 0;
        }
        if (wait_started) {
            start += SDL_GetTicks()-wait_started;
            wait_started=0;
        }
    }
    switch(phase) {
    case 0: {
        if(lv_image_cache_is_enabled()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_TAXI);
        original_callbacks = falafel_gui.callbacks;
        falafel_gui_callbacks_t callbacks = original_callbacks;
        callbacks.on_personality_changed = personality_changed;
        falafel_gui_set_callbacks(&callbacks);
        lv_obj_add_event_cb(character(), frame_drawn, LV_EVENT_DRAW_POST, NULL);
        heartbeat_timer = lv_timer_create(heartbeat, 10, NULL);
        break;
    }
    case 1: if(capture(d,"home")) goto failure; pointer(w,SDL_MOUSEBUTTONDOWN,330,240); break;
    case 2: pointer(w,SDL_MOUSEMOTION,130,240); break;
    case 3: pointer(w,SDL_MOUSEBUTTONUP,130,240); break;
    case 4:
        if(!falafel_gui.personality_animating || changes ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_TAXI) goto failure;
        if(falafel_gui_next_personality() || falafel_gui_previous_personality()) goto failure;
        wheel(w,-2.0f); /* Overlapping trackpad swipe must be discarded. */
        break;
    case 5:
        if(falafel_gui.personality_animating || changes!=1 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK) goto failure;
        drawn_frames=0;
        wheel(w,-2.0f); break;
    case 6: pointer(w,SDL_MOUSEBUTTONDOWN,233,114); break;
    case 7: pointer(w,SDL_MOUSEBUTTONUP,233,114); break;
    case 8:
        if(!falafel_gui.personality_animating || falafel_gui_is_home_active() ||
           lv_obj_has_flag(falafel_gui.messages,LV_OBJ_FLAG_HIDDEN)) goto failure;
        if(capture(d,"notification")) goto failure;
        falafel_gui_show_notification("Notifica durante rotazione");
        if(lv_obj_has_flag(falafel_gui.notification,LV_OBJ_FLAG_HIDDEN) || capture(d,"banner")) goto failure;
        break;
    case 9:
        if(falafel_gui.personality_animating || changes!=2 ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_REMINDER) goto failure;
        pointer(w,SDL_MOUSEBUTTONDOWN,233,354); break;
    case 10: pointer(w,SDL_MOUSEBUTTONUP,233,354); break;
    case 11:
        if(!falafel_gui_is_home_active()) goto failure;
        drawn_frames=0;
        wheel(w,2.0f); break;
    case 12:
        if(!falafel_gui.personality_animating ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_REMINDER) goto failure;
        if(capture(d,"rotation")) goto failure;
        break;
    case 13:
        if(falafel_gui.personality_animating || changes!=3 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK) goto failure;
        if(capture(d,"home_after_rotation")) goto failure;
        break;
    case 14:
        if(!lv_obj_has_flag(falafel_gui.notification,LV_OBJ_FLAG_HIDDEN)) goto failure;
        /* Remote state cancels pending local choice without a stale callback. */
        if(!falafel_gui_next_personality()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        if(falafel_gui.personality_animating || !falafel_gui_next_personality()) goto failure;
        if(!falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_TAXI)) goto failure;
        break;
    case 15: {
        if(falafel_gui.personality_animating || changes!=3 ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_TAXI ||
           lv_image_get_src(character())!=&falafel_asset_character ||
           heartbeat_count<100 || heartbeat_gap>200) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_TAXI);
        if(falafel_gui_previous_personality()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_REMINDER);
        if(falafel_gui_next_personality()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        printf("[UI] heartbeat max gap %u ms; LVGL heap peak %zu bytes\n",
               heartbeat_gap, memory.max_used);
        lv_timer_delete(heartbeat_timer);
        falafel_gui_set_callbacks(&original_callbacks);
        puts("[UI] PASS: 24 rendered frames both directions, deferred selection, swipe lock, "
             "messages during rotation, banner timeout, remote cancellation, static restore");
        return 1;
    }
    }
    ++phase; return 0;
failure:
    fprintf(stderr,"[UI] FAIL phase %u, personality %u, frame %u, draws %06x, callbacks %u, gap %u\n",
            phase,(unsigned)falafel_gui.personality, falafel_gui.rotation_frame,
            drawn_frames, changes, heartbeat_gap);
    return -1;
}
