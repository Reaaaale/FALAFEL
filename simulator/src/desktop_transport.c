#include "desktop_transport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000U+(uint64_t)t.tv_nsec/1000000U;}
static void disconnect(desktop_transport_t *t){if(t->fd>=0)close(t->fd);t->fd=-1;t->input_length=0;t->reconnect_after_ms=monotonic_ms()+1000U;}
void desktop_transport_init(desktop_transport_t *t,const char *host,uint16_t port,uint32_t own,uint32_t peer,desktop_event_fn cb,void *data){
    memset(t,0,sizeof(*t));t->fd=-1;snprintf(t->host,sizeof(t->host),"%s",host);t->port=port;t->device_id=own;t->peer_id=peer;t->on_event=cb;t->user_data=data;
}
static void connect_relay(desktop_transport_t *t){
    if(t->fd>=0||monotonic_ms()<t->reconnect_after_ms)return;char port[8];snprintf(port,sizeof(port),"%u",t->port);struct addrinfo hints={0},*addresses=NULL;hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(t->host,port,&hints,&addresses)!=0){t->reconnect_after_ms=monotonic_ms()+1000U;return;}
    int fd=-1;for(struct addrinfo *a=addresses;a;a=a->ai_next){fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd>=0&&connect(fd,a->ai_addr,a->ai_addrlen)==0)break;if(fd>=0)close(fd);fd=-1;}freeaddrinfo(addresses);
    if(fd<0){t->reconnect_after_ms=monotonic_ms()+1000U;return;}char hello[80];int n=snprintf(hello,sizeof(hello),"HELLO %u %u\n",t->device_id,t->peer_id);
    if(send(fd,hello,(size_t)n,0)!=n){close(fd);return;}fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);t->fd=fd;
}
bool desktop_transport_send(void *context,const falafel_event_t *event){desktop_transport_t *t=context;if(t->fd<0)return false;uint8_t wire[FALAFEL_MAX_EVENT_SIZE],frame[FALAFEL_MAX_EVENT_SIZE+4];size_t n=0;
    if(!falafel_event_serialize(event,wire,sizeof(wire),&n))return false;uint32_t be=htonl((uint32_t)n);memcpy(frame,&be,4);memcpy(frame+4,wire,n);size_t sent=0;
    while(sent<n+4){ssize_t count=send(t->fd,frame+sent,n+4-sent,MSG_NOSIGNAL);if(count<0&&errno==EINTR)continue;if(count<=0){disconnect(t);return false;}sent+=(size_t)count;}return true;}
void desktop_transport_poll(desktop_transport_t *t){connect_relay(t);if(t->fd<0)return;for(;;){ssize_t n=recv(t->fd,t->input+t->input_length,sizeof(t->input)-t->input_length,0);if(n>0)t->input_length+=(size_t)n;else if(n==0){disconnect(t);return;}else if(errno==EAGAIN||errno==EWOULDBLOCK)break;else if(errno!=EINTR){disconnect(t);return;}}
    while(t->input_length>=4){uint32_t size;memcpy(&size,t->input,4);size=ntohl(size);if(size>FALAFEL_MAX_EVENT_SIZE||size<40){disconnect(t);return;}if(t->input_length<size+4)return;falafel_event_t event;if(falafel_event_deserialize(t->input+4,size,&event)&&t->on_event)t->on_event(t->user_data,&event);memmove(t->input,t->input+4+size,t->input_length-4-size);t->input_length-=4+size;}}

typedef struct {uint8_t magic[8];uint32_t version;uint8_t personality;uint8_t pad[3];uint64_t revision;uint32_t author;uint32_t queue_count;uint32_t seen_count;falafel_queue_item_t queue[FALAFEL_APP_QUEUE_CAPACITY];uint64_t seen_ids[FALAFEL_APP_SEEN_CAPACITY];uint32_t seen_sources[FALAFEL_APP_SEEN_CAPACITY];} state_file_t;
bool desktop_state_load(const char *path,falafel_app_t *app){FILE *f=fopen(path,"rb");if(!f)return false;state_file_t s;bool ok=fread(&s,sizeof(s),1,f)==1&&memcmp(s.magic,"FALSTATE",8)==0&&s.version==1&&s.personality<3&&s.queue_count<=FALAFEL_APP_QUEUE_CAPACITY&&s.seen_count<=FALAFEL_APP_SEEN_CAPACITY;fclose(f);uint8_t wire[FALAFEL_MAX_EVENT_SIZE];size_t n;
    for(size_t i=0;ok&&i<s.queue_count;++i)ok=s.queue[i].state>=FALAFEL_MESSAGE_QUEUED&&s.queue[i].state<=FALAFEL_MESSAGE_DEVICE_RECEIVED&&falafel_event_serialize(&s.queue[i].event,wire,sizeof(wire),&n);
    if(!ok)return false;app->personality=s.personality;app->personality_revision=s.revision;app->personality_author=s.author;app->queue_count=s.queue_count;app->seen_count=s.seen_count;memcpy(app->queue,s.queue,sizeof(app->queue));memcpy(app->seen_ids,s.seen_ids,sizeof(app->seen_ids));memcpy(app->seen_sources,s.seen_sources,sizeof(app->seen_sources));return true;}
bool desktop_state_save(const char *path,const falafel_app_t *app){char temporary[512];if(snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary))return false;state_file_t s={.magic={'F','A','L','S','T','A','T','E'},.version=1,.personality=app->personality,.revision=app->personality_revision,.author=app->personality_author,.queue_count=(uint32_t)app->queue_count,.seen_count=(uint32_t)app->seen_count};memcpy(s.queue,app->queue,sizeof(s.queue));memcpy(s.seen_ids,app->seen_ids,sizeof(s.seen_ids));memcpy(s.seen_sources,app->seen_sources,sizeof(s.seen_sources));FILE *f=fopen(temporary,"wb");if(!f)return false;bool ok=fwrite(&s,sizeof(s),1,f)==1&&fflush(f)==0&&fsync(fileno(f))==0&&fclose(f)==0;if(!ok){unlink(temporary);return false;}return rename(temporary,path)==0;}
