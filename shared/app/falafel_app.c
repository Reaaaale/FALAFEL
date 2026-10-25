#include "falafel_app.h"
#include <string.h>
static falafel_app_result_t enqueue(falafel_app_t *a,const falafel_event_t *e){
    if(a->queue_count==FALAFEL_APP_QUEUE_CAPACITY)return FALAFEL_APP_QUEUE_FULL;
    falafel_queue_item_t item={.event=*e,.state=FALAFEL_MESSAGE_QUEUED};a->queue[a->queue_count++]=item;return FALAFEL_APP_OK;
}
falafel_app_result_t falafel_app_init(falafel_app_t *a,const falafel_app_config_t *c){
    if(!a||!c||!c->next_message_id||!c->current_time||!c->device_id||!c->peer_id||c->device_id==c->peer_id)return FALAFEL_APP_INVALID_ARGUMENT;
    falafel_app_t t={0};t.config=*c;t.personality_author=c->device_id;*a=t;return FALAFEL_APP_OK;
}
falafel_app_result_t falafel_app_create_text(falafel_app_t *a,const char *s,uint64_t *id){
    if(!a||!s)return FALAFEL_APP_INVALID_ARGUMENT;size_t n=strlen(s);if(!n)return FALAFEL_APP_EMPTY_TEXT;
    if(n>FALAFEL_MAX_PAYLOAD_SIZE)return FALAFEL_APP_TEXT_TOO_LONG;if(a->queue_count==FALAFEL_APP_QUEUE_CAPACITY)return FALAFEL_APP_QUEUE_FULL;
    falafel_event_t e={.source_id=a->config.device_id,.destination_id=a->config.peer_id,.type=FALAFEL_EVENT_TEXT,.payload_length=(uint16_t)n};
    if(!a->config.next_message_id(a->config.user_data,&e.id)||!e.id||!a->config.current_time(a->config.user_data,&e.timestamp))return FALAFEL_APP_PROVIDER_FAILED;
    memcpy(e.payload,s,n);falafel_app_result_t r=enqueue(a,&e);if(r==FALAFEL_APP_OK&&id)*id=e.id;return r;
}
falafel_app_result_t falafel_app_set_personality(falafel_app_t *a,uint8_t p){
    if(!a||p>=3)return FALAFEL_APP_INVALID_ARGUMENT;if(a->queue_count==FALAFEL_APP_QUEUE_CAPACITY)return FALAFEL_APP_QUEUE_FULL;
    falafel_event_t e={.source_id=a->config.device_id,.destination_id=a->config.peer_id,.type=FALAFEL_EVENT_PERSONALITY,.personality=p,.revision=a->personality_revision+1};
    if(!a->config.next_message_id(a->config.user_data,&e.id)||!e.id||!a->config.current_time(a->config.user_data,&e.timestamp))return FALAFEL_APP_PROVIDER_FAILED;
    falafel_app_result_t r=enqueue(a,&e);if(r==FALAFEL_APP_OK){a->personality=p;a->personality_revision=e.revision;a->personality_author=a->config.device_id;}return r;
}
static bool seen(const falafel_app_t *a,const falafel_event_t *e){for(size_t i=0;i<a->seen_count;++i)if(a->seen_ids[i]==e->id&&a->seen_sources[i]==e->source_id)return true;return false;}
static void remember(falafel_app_t *a,const falafel_event_t *e){size_t i=a->seen_count<FALAFEL_APP_SEEN_CAPACITY?a->seen_count++:(size_t)(e->id%FALAFEL_APP_SEEN_CAPACITY);a->seen_ids[i]=e->id;a->seen_sources[i]=e->source_id;}
falafel_app_result_t falafel_app_transition(falafel_app_t *a,uint64_t id,falafel_message_state_t next){
    if(!a)return FALAFEL_APP_INVALID_ARGUMENT;for(size_t i=0;i<a->queue_count;++i){if(a->queue[i].event.id!=id)continue;falafel_message_state_t cur=a->queue[i].state;
        if(next<=cur||next>FALAFEL_MESSAGE_READ||(cur==FALAFEL_MESSAGE_QUEUED&&next!=FALAFEL_MESSAGE_RELAY_ACCEPTED)||(cur==FALAFEL_MESSAGE_RELAY_ACCEPTED&&next!=FALAFEL_MESSAGE_DEVICE_RECEIVED)||(cur==FALAFEL_MESSAGE_DEVICE_RECEIVED&&next!=FALAFEL_MESSAGE_READ))return FALAFEL_APP_INVALID_TRANSITION;
        a->queue[i].state=next;return FALAFEL_APP_OK;}return FALAFEL_APP_NOT_FOUND;
}
falafel_app_result_t falafel_app_receive(falafel_app_t *a,const falafel_event_t *e,bool *changed){
    if(!a||!e||e->destination_id!=a->config.device_id)return FALAFEL_APP_INVALID_ARGUMENT;if(changed)*changed=false;
    if(e->type==FALAFEL_EVENT_ACK)return falafel_app_transition(a,e->id,e->ack_phase==FALAFEL_ACK_RELAY_ACCEPTED?FALAFEL_MESSAGE_RELAY_ACCEPTED:FALAFEL_MESSAGE_DEVICE_RECEIVED);
    if(seen(a,e))return FALAFEL_APP_DUPLICATE;remember(a,e);
    if(e->type==FALAFEL_EVENT_PERSONALITY){bool newer=e->revision>a->personality_revision||(e->revision==a->personality_revision&&e->source_id>a->personality_author);if(!newer)return FALAFEL_APP_IGNORED;
        a->personality=e->personality;a->personality_revision=e->revision;a->personality_author=e->source_id;if(changed)*changed=true;}return FALAFEL_APP_OK;
}
falafel_app_result_t falafel_app_flush(falafel_app_t *a){if(!a)return FALAFEL_APP_INVALID_ARGUMENT;if(!a->config.send_event)return FALAFEL_APP_OK;
    for(size_t i=0;i<a->queue_count;++i)if(a->queue[i].state==FALAFEL_MESSAGE_QUEUED&&!a->config.send_event(a->config.user_data,&a->queue[i].event))return FALAFEL_APP_PROVIDER_FAILED;return FALAFEL_APP_OK;}
const falafel_queue_item_t *falafel_app_queue_at(const falafel_app_t *a,size_t i){return a&&i<a->queue_count?&a->queue[i]:NULL;}
void falafel_app_remove_delivered(falafel_app_t *a){if(!a)return;size_t w=0;for(size_t r=0;r<a->queue_count;++r)if(a->queue[r].state<FALAFEL_MESSAGE_DEVICE_RECEIVED)a->queue[w++]=a->queue[r];a->queue_count=w;}
