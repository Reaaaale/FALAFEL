#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "SDL2/SDL.h"
#include "desktop_transport.h"
#include "falafel_app.h"
#include "falafel_gui.h"
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "trackpad_navigation.h"

enum { DISPLAY_WIDTH=320, DISPLAY_HEIGHT=480 };
typedef struct { falafel_app_t app; desktop_transport_t transport; uint64_t counter;
    char state_path[384]; uint32_t device_id; uint16_t unread; } simulator_t;

static bool next_id(void *data,uint64_t *id){simulator_t *s=data;*id=((uint64_t)s->device_id<<32)|++s->counter;return true;}
static bool current_time(void *data,uint64_t *value){(void)data;time_t now=time(NULL);if(now==(time_t)-1)return false;*value=(uint64_t)now;return true;}
static bool send_event(void *data,const falafel_event_t *event){simulator_t *s=data;return desktop_transport_send(&s->transport,event);}
static void save(simulator_t *s){if(!desktop_state_save(s->state_path,&s->app))fprintf(stderr,"[STATE] Unable to save %s\n",s->state_path);}

static void send_device_ack(simulator_t *s,const falafel_event_t *received){falafel_event_t ack={.source_id=s->app.config.device_id,.destination_id=received->source_id,.id=received->id,.timestamp=received->timestamp,.type=FALAFEL_EVENT_ACK,.ack_phase=FALAFEL_ACK_DEVICE_RECEIVED};desktop_transport_send(&s->transport,&ack);}
static void incoming(void *data,const falafel_event_t *event){simulator_t *s=data;bool changed=false;falafel_app_result_t result=falafel_app_receive(&s->app,event,&changed);
    if(event->type==FALAFEL_EVENT_ACK&&result==FALAFEL_APP_OK){if(event->ack_phase==FALAFEL_ACK_RELAY_ACCEPTED){puts("[STATE] RELAY_ACCEPTED");falafel_gui_show_notification("Relay: accettato");}else{puts("[STATE] DEVICE_RECEIVED");falafel_gui_show_notification("Dispositivo: ricevuto");falafel_app_remove_delivered(&s->app);}save(s);return;}
    if(result!=FALAFEL_APP_OK&&result!=FALAFEL_APP_IGNORED&&result!=FALAFEL_APP_DUPLICATE)return;
    if(result==FALAFEL_APP_OK&&event->type==FALAFEL_EVENT_TEXT){char text[FALAFEL_MAX_PAYLOAD_SIZE+1];memcpy(text,event->payload,event->payload_length);text[event->payload_length]='\0';falafel_gui_add_received_message(text);if(s->unread<UINT16_MAX)++s->unread;falafel_gui_set_unread_count(s->unread);}
    if(changed)falafel_gui_set_personality_remote((falafel_personality_t)s->app.personality);
    if(event->type!=FALAFEL_EVENT_ACK)send_device_ack(s,event);save(s);
}
static bool gui_send(const char *text,void *data){simulator_t *s=data;if(falafel_app_create_text(&s->app,text,NULL)!=FALAFEL_APP_OK)return false;save(s);return true;}
static void gui_personality(falafel_personality_t value,void *data){simulator_t *s=data;if(falafel_app_set_personality(&s->app,(uint8_t)value)!=FALAFEL_APP_OK){falafel_gui_show_notification("Cambio non accodato");return;}save(s);}
static void gui_messages(void *data){simulator_t *s=data;s->unread=0;}
static const char *argument(int argc,char **argv,const char *name,const char *fallback){for(int i=1;i+1<argc;++i)if(strcmp(argv[i],name)==0)return argv[i+1];return fallback;}
int main(int argc,char **argv){
    const char *device_text=argument(argc,argv,"--device",getenv("FALAFEL_DEVICE_ID"));const char *peer_text=argument(argc,argv,"--peer",getenv("FALAFEL_PEER_ID"));
    const char *host=argument(argc,argv,"--host",getenv("FALAFEL_RELAY_HOST"));const char *port_text=argument(argc,argv,"--port",getenv("FALAFEL_RELAY_PORT"));const char *state=argument(argc,argv,"--data",getenv("FALAFEL_DATA_FILE"));
    if(!device_text||!peer_text){fprintf(stderr,"Usage: %s --device ID --peer ID [--host HOST] [--port PORT] [--data FILE]\n",argv[0]);return EXIT_FAILURE;}
    simulator_t simulator={.device_id=(uint32_t)strtoul(device_text,NULL,10)};uint32_t peer=(uint32_t)strtoul(peer_text,NULL,10);uint16_t port=(uint16_t)strtoul(port_text?port_text:"7777",NULL,10);
    snprintf(simulator.state_path,sizeof(simulator.state_path),"%s",state?state:"falafel-device.state");desktop_transport_init(&simulator.transport,host?host:"127.0.0.1",port,simulator.device_id,peer,incoming,&simulator);
    falafel_app_config_t config={.next_message_id=next_id,.current_time=current_time,.send_event=send_event,.user_data=&simulator,.device_id=simulator.device_id,.peer_id=peer};
    if(falafel_app_init(&simulator.app,&config)!=FALAFEL_APP_OK)return EXIT_FAILURE;desktop_state_load(simulator.state_path,&simulator.app);
    for(size_t i=0;i<simulator.app.queue_count;++i){uint64_t low=simulator.app.queue[i].event.id&UINT64_C(0xffffffff);if(low>simulator.counter)simulator.counter=low;}
    lv_init();lv_display_t *display=lv_sdl_window_create(DISPLAY_WIDTH,DISPLAY_HEIGHT);if(!display)return EXIT_FAILURE;lv_sdl_window_set_title(display,device_text);lv_sdl_mouse_create();SDL_Window *window=lv_sdl_window_get_window(display);trackpad_navigation_init(window?SDL_GetWindowID(window):0,false);
    falafel_gui_callbacks_t callbacks={.on_send_message=gui_send,.on_personality_changed=gui_personality,.on_messages_opened=gui_messages,.user_data=&simulator};falafel_gui_set_callbacks(&callbacks);falafel_gui_create();falafel_gui_set_personality_remote((falafel_personality_t)simulator.app.personality);falafel_gui_set_battery_percent(82);
    uint32_t last_flush=0;for(;;){desktop_transport_poll(&simulator.transport);falafel_gui_set_wifi_connected(simulator.transport.fd>=0);falafel_gui_set_peer_connected(simulator.transport.fd>=0);uint32_t now=SDL_GetTicks();if(now-last_flush>=1000){falafel_app_flush(&simulator.app);last_flush=now;}uint32_t wait=lv_timer_handler();trackpad_navigation_process();SDL_Delay(wait<1?1:wait>16?16:wait);}return 0;
}
