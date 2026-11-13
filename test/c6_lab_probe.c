#define _POSIX_C_SOURCE 200809L
#include "lab_messaging.h"
#include "relay_tcp.h"
#include "relay_tls.h"
#include "falafel_storage_espidf.h"
#include "nvs_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static falafel_lab lab;
static falafel_storage_espidf store;
static unsigned events[5];
static uint64_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void event(void *u, falafel_lab_event e, const uint8_t id[16])
{
    (void)u;(void)id;++events[e];
    printf("EVENT %u\n",(unsigned)e);fflush(stdout);
}
static unsigned char *pem(const char *path, size_t *size)
{
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    unsigned char *bytes=calloc(1,16385); if(!bytes) { fclose(f);return NULL; }
    size_t n=fread(bytes,1,16384,f); int extra=fgetc(f);fclose(f);
    if(!n || extra!=EOF) { free(bytes);return NULL; }
    *size=n+1;return bytes;
}
static void close_link(int *fd, falafel_tls **tls)
{
    falafel_tls_free(*tls);*tls=NULL;
    if(*fd>=0) close(*fd);
    *fd=-1;
}
int main(int argc,char **argv)
{
    if(argc!=4 && argc!=8) return 2;
    unsigned char *ca=NULL,*cert=NULL,*key=NULL;size_t ca_n=0,cert_n=0,key_n=0;
    if(argc==8) {
        ca=pem(argv[4],&ca_n);cert=pem(argv[5],&cert_n);key=pem(argv[6],&key_n);
        if(!ca || !cert || !key) return 2;
    }
    falafel_tls *tls=NULL;
    uint8_t credential[64];FILE *file=fopen(argv[1],"rb");
    if(!file) return 2;
    size_t n=fread(credential,1,sizeof(credential),file);int extra=fgetc(file);fclose(file);
    if(n!=64 || extra!=EOF) return 2;
    fake_nvs_reset();assert(!falafel_storage_espidf_open(&store,"falafel_data",true));
    falafel_relay_io io;falafel_relay_io_init(&io,-1,falafel_lab_receive,&lab);
    assert(!falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),credential,1,2,true,
        "Ciao dal Falafel ESP32-C6",falafel_relay_io_send,&io,event,NULL));
    falafel_crypto_secure_zero(credential,sizeof(credential));
    int fd=-1;bool cut_upload=false,cut_delivered=false;unsigned connections=0;
    uint64_t start=now_ms(),connected_at=0,delivery_at=0;
    while(now_ms()-start<6500) {
        if(fd<0) {
            if(argc==8) tls=falafel_relay_tls_open(argv[2],(uint16_t)strtoul(argv[3],NULL,10),
                argv[7],1,2,ca,ca_n,cert,cert_n,key,key_n,&fd);
            else fd=falafel_relay_tcp_open(argv[2],(uint16_t)strtoul(argv[3],NULL,10),1,2);
            if(fd<0) return 3;
            falafel_relay_io_init(&io,fd,falafel_lab_receive,&lab);io.io_chunk=3;
            if(tls) falafel_relay_tls_attach(&io,tls);
            falafel_mail_link_connected(&lab.link);connected_at=now_ms();++connections;
        }
        uint64_t now=now_ms();
        if(!falafel_relay_io_poll(&io,now)) { close_link(&fd,&tls);continue; }
        falafel_mail_link_tick(&lab.link,now);
        if(!cut_upload && now-connected_at>=15) {
            close_link(&fd,&tls);cut_upload=true;continue;
        }
        if(events[LAB_DELIVERED] && !delivery_at) delivery_at=now;
        if(delivery_at && !cut_delivered && now-delivery_at>100) {
            close_link(&fd,&tls);cut_delivered=true;
            /* Reopen the real C6 worker against its persistent NVS model. */
            uint8_t raw[64];memcpy(raw,lab.identity.local_private,32);memcpy(raw+32,lab.identity.peer_public,32);
            falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));fake_nvs_reboot();
            assert(!falafel_storage_espidf_open(&store,"falafel_data",false));
            assert(!falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),raw,1,2,false,
                 "must not enqueue again",falafel_relay_io_send,&io,event,NULL));
            falafel_crypto_secure_zero(raw,sizeof(raw));continue;
        }
        struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);
    }
    close_link(&fd,&tls);
    if(key) falafel_crypto_secure_zero(key,key_n);
    free(key);free(cert);free(ca);
    bool ok=events[LAB_STORED]==1 && events[LAB_DELIVERED]==1 && events[LAB_RECEIVED]==1 &&
        events[LAB_RESTORED]==1 && connections>=3 && lab.mailbox.outbox[0].state==2 && !lab.mailbox.outbox[1].state;
    printf("RESULT stored=%u delivered=%u received=%u restored=%u connections=%u\n",
        events[0],events[2],events[1],events[3],connections);
    falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));fake_nvs_reset();
    return ok?0:4;
}
