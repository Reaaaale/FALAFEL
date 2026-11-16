#include "ui_smoke.h"
#include "falafel_gui_internal.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef FALAFEL_GUI_HEARTBEAT_LIMIT_MS
#define FALAFEL_GUI_HEARTBEAT_LIMIT_MS 200
#endif

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
static void flipped_wheel(SDL_Window *w, float x)
{
    SDL_Event e={0}; e.type=SDL_MOUSEWHEEL; e.wheel.windowID=SDL_GetWindowID(w);
    e.wheel.timestamp=SDL_GetTicks(); e.wheel.preciseX=x; e.wheel.x=(int)x;
    e.wheel.direction=SDL_MOUSEWHEEL_FLIPPED; SDL_PushEvent(&e);
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
static uint32_t first_frame_ms, timed_start_ms;
static bool invalid_frame_order, invalid_duration;
static int rotation_direction = -1;
static int last_drawn_frame = -1;
static falafel_personality_t observed_source, observed_outfit, observed_target;
static uint8_t initial_pose, interrupted_pose;
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
    if (!drawn_frames) {
        first_frame_ms = SDL_GetTicks();
        observed_source = observed_outfit = falafel_gui.personality;
        observed_target = (falafel_personality_t)((int)observed_source - rotation_direction);
    }
    if (falafel_gui.personality != observed_source) invalid_frame_order = true;
    if (last_drawn_frame >= 0 && last_drawn_frame != falafel_gui.rotation_frame &&
        falafel_gui.rotation_frame == FALAFEL_ROTATION_BACK_FRAME)
        observed_outfit = observed_target;
    uint32_t bit = 1u << falafel_gui.rotation_frame;
    if (last_drawn_frame >= 0 && last_drawn_frame != falafel_gui.rotation_frame &&
        falafel_gui.rotation_frame != (last_drawn_frame + rotation_direction +
            FALAFEL_ROTATION_FRAME_COUNT) % FALAFEL_ROTATION_FRAME_COUNT)
        invalid_frame_order = true;
    last_drawn_frame = falafel_gui.rotation_frame;
    if (lv_image_get_src(character()) != &falafel_asset_rotation[observed_outfit][falafel_gui.rotation_frame])
        invalid_frame_order = true;
    drawn_frames |= bit;
}

static void observe_rotation(int direction)
{
    drawn_frames=0;
    last_drawn_frame=-1;
    invalid_frame_order=false;
    rotation_direction=direction;
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
    if (falafel_gui.personality_animating || falafel_gui.personality != selected ||
        lv_image_get_src(character()) != &falafel_asset_rotation[selected][falafel_gui.rotation_frame])
        invalid_frame_order = true;
    ++changes;
    uint32_t elapsed = SDL_GetTicks() - first_frame_ms;
    printf("[UI] rotation completed after %u ms from first draw (target %u ms)\n",
           elapsed, FALAFEL_ROTATION_DURATION_MS);
    /* Keep normal playback near its target, including render/event-loop cost.
     * Sanitizer-instrumented PNG decoding is checked for liveness, not speed. */
    if (FALAFEL_GUI_HEARTBEAT_LIMIT_MS == 200 && elapsed > 450) invalid_duration=true;
    if (timed_start_ms) {
        uint32_t total = SDL_GetTicks() - timed_start_ms;
        printf("[UI] full 24-step rotation: %u ms from request\n", total);
        if (FALAFEL_GUI_HEARTBEAT_LIMIT_MS == 200 &&
            (total < FALAFEL_ROTATION_DURATION_MS || total > FALAFEL_ROTATION_DURATION_MS + 80))
            invalid_duration=true;
        timed_start_ms=0;
    }
    if (original_callbacks.on_personality_changed)
        original_callbacks.on_personality_changed(selected, data);
}

int ui_smoke_step(lv_display_t *d, SDL_Window *w)
{
    static unsigned phase; static Uint32 start, wait_started;
    static const Uint32 times[]={0,200,300,400,450,1400,1430,1460,1490,2400,
                                 2500,2700,2750,3800,4600,5500,
                                 5600,5700,5800,5850,6800,6900,7000,7100,
                                 7200,7300,8300,8500,8560,8680,8750,9800,
                                 10800,11000,11200,12200,12400,13400,13600,14600,14800,15800};
    const uint32_t all_frames = (1u << FALAFEL_ROTATION_FRAME_COUNT) - 1;
    if(!start) start=SDL_GetTicks();
    if(phase>=sizeof(times)/sizeof(times[0])) return 1;
    if(SDL_GetTicks()-start<times[phase]) return 0;
    int width,height; SDL_GetWindowSize(w,&width,&height);
    if(width!=466 || height!=466) goto failure;
    /* Instrumented decoders are slower. Wait for completion without assuming
     * desktop release timing, while retaining a bounded failure deadline. */
    if (phase==5 || phase==9 || phase==13 || phase==20 || phase==26 ||
        phase==30 || phase==32 || phase==35 || phase==37 || phase==39 || phase==41) {
        if (falafel_gui.personality_animating) {
            if (phase==30) wheel(w,0.2f); /* A busy gesture's tail spans completion. */
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
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK || falafel_gui.rotation_frame!=0 ||
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][0]) goto failure;
        drawn_frames=0; last_drawn_frame=-1;
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
        drawn_frames=0; last_drawn_frame=-1; rotation_direction=1;
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
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][falafel_gui.rotation_frame]) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_TAXI);
        if(falafel_gui_previous_personality()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_REMINDER);
        if(falafel_gui_next_personality()) goto failure;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        break;
    }
    case 16:
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_TAXI);
        initial_pose=falafel_gui.rotation_frame;
        observe_rotation(-1);
        pointer(w,SDL_MOUSEBUTTONDOWN,330,240); break;
    case 17: pointer(w,SDL_MOUSEMOTION,130,240); break;
    case 18: pointer(w,SDL_MOUSEBUTTONUP,130,240); break;
    case 19:
        if(!falafel_gui.personality_animating || changes!=3) goto failure;
        /* Starts while busy, releases after completion: must never queue. */
        pointer(w,SDL_MOUSEBUTTONDOWN,330,240);
        for(unsigned i=0;i<8;++i)
            if(falafel_gui_next_personality() || falafel_gui_previous_personality()) goto failure;
        break;
    case 20:
        if(changes!=4 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK ||
           falafel_gui.rotation_frame!=initial_pose) goto failure;
        break;
    case 21: pointer(w,SDL_MOUSEMOTION,130,240); break;
    case 22: pointer(w,SDL_MOUSEBUTTONUP,130,240); break;
    case 23:
        if(falafel_gui.personality_animating || changes!=4 ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK) goto failure;
        observe_rotation(1);
        pointer(w,SDL_MOUSEBUTTONDOWN,130,240); break;
    case 24: pointer(w,SDL_MOUSEMOTION,330,240); break;
    case 25: pointer(w,SDL_MOUSEBUTTONUP,330,240); break;
    case 26:
        if(changes!=5 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_TAXI ||
           falafel_gui.rotation_frame!=initial_pose ||
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][initial_pose]) goto failure;
        if(capture(d,"home_left_right")) goto failure;
        break;
    case 27:
        observe_rotation(-1);
        timed_start_ms=SDL_GetTicks();
        if(!falafel_gui_next_personality()) goto failure;
        break;
    case 28:
        if(!falafel_gui.personality_animating || changes!=5) goto failure;
        wheel(w,0.2f); break; /* A new subthreshold burst begins during playback. */
    case 29: wheel(w,0.2f); break;
    case 30:
        if(changes!=6 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK) goto failure;
        wheel(w,2.0f); /* Even a threshold-crossing inertial tail is not a new swipe. */
        break;
    case 31:
        if(falafel_gui.personality_animating || changes!=6 ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_COOK) goto failure;
        observe_rotation(1);
        flipped_wheel(w,-2.0f); /* Physical right with natural scrolling. */
        break;
    case 32:
        if(changes!=7 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_TAXI ||
           falafel_gui.rotation_frame!=initial_pose) goto failure;
        break;
    case 33:
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        observe_rotation(-1);
        if(!falafel_gui_next_personality()) goto failure;
        break;
    case 34:
        if(!falafel_gui.personality_animating || falafel_gui.rotation_frame==initial_pose) goto failure;
        interrupted_pose=falafel_gui.rotation_frame;
        falafel_gui_set_personality_remote(FALAFEL_PERSONALITY_COOK);
        if(falafel_gui.personality_animating || changes!=7 ||
           falafel_gui.rotation_frame!=interrupted_pose ||
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][interrupted_pose]) goto failure;
        observe_rotation(1);
        timed_start_ms=SDL_GetTicks();
        if(!falafel_gui_previous_personality() || falafel_gui.rotation_frame!=interrupted_pose ||
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][interrupted_pose]) goto failure;
        if(capture(d,"rotation_reverse")) goto failure;
        break;
    case 35:
        if(changes!=8 || invalid_frame_order || drawn_frames!=all_frames ||
           falafel_gui.personality!=FALAFEL_PERSONALITY_TAXI ||
           falafel_gui.rotation_frame!=interrupted_pose) goto failure;
        break;
    case 36:
        observe_rotation(-1);
        timed_start_ms=SDL_GetTicks();
        if(!falafel_gui_next_personality() || falafel_gui.rotation_frame!=interrupted_pose) goto failure;
        break;
    case 37: {
        if(changes!=9 || invalid_frame_order || invalid_duration || drawn_frames!=all_frames ||
           falafel_gui.personality_animating || falafel_gui.personality!=FALAFEL_PERSONALITY_COOK ||
           falafel_gui.rotation_frame!=interrupted_pose ||
           lv_image_get_src(character())!=&falafel_asset_rotation[falafel_gui.personality][interrupted_pose] ||
           heartbeat_count<100 || heartbeat_gap>FALAFEL_GUI_HEARTBEAT_LIMIT_MS) goto failure;
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        printf("[UI] heartbeat max gap %u ms (limit %u); LVGL heap peak %zu bytes\n",
               heartbeat_gap, FALAFEL_GUI_HEARTBEAT_LIMIT_MS, memory.max_used);
        break;
    }
    case 38:
        observe_rotation(-1);
        timed_start_ms=SDL_GetTicks();
        if (!falafel_gui_next_personality()) goto failure;
        break;
    case 39:
        if (changes!=10 || invalid_frame_order || invalid_duration || drawn_frames!=all_frames ||
            falafel_gui.personality!=FALAFEL_PERSONALITY_REMINDER ||
            falafel_gui.rotation_frame!=interrupted_pose) goto failure;
        if (capture(d,"home_reminder")) goto failure;
        observe_rotation(1);
        timed_start_ms=SDL_GetTicks();
        if (!falafel_gui_previous_personality()) goto failure;
        break;
    case 40:
        break;
    case 41: {
        if (changes!=11 || invalid_frame_order || invalid_duration || drawn_frames!=all_frames ||
            falafel_gui.personality!=FALAFEL_PERSONALITY_COOK ||
            falafel_gui.rotation_frame!=interrupted_pose ||
            lv_image_get_src(character())!=&falafel_asset_rotation[FALAFEL_PERSONALITY_COOK][interrupted_pose]) goto failure;
        if (capture(d,"home_cook")) goto failure;
        lv_timer_delete(heartbeat_timer);
        falafel_gui_set_callbacks(&original_callbacks);
        puts("[UI] PASS: opposite cyclic frame order, left/right pose continuity, "
             "rapid swipe/held drag/inertial tail lock, single callback, flipped wheel, "
             "messages during rotation, remote cancellation without pose reset, "
             "all adjacent outfit transitions switch at rear with deferred application state");
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
