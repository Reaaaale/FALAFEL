#define _POSIX_C_SOURCE 200809L
#include "desktop_transport.h"
#include "falafel_mail_link.h"
#include "falafel_mailbox_posix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static falafel_crypto_context_t crypto;
static falafel_pairing_persistent_state identity;
static falafel_mailbox mailbox;
static falafel_mail_link link_state;
static desktop_transport_t transport;
static char base[1024];
static int crash_effect, bad_effect;
static unsigned rejected, full, duplicates;
static uint64_t now_ms(void)
{ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000; }
static int effect(void *u, const uint8_t sender[32], const uint8_t id[16], const uint8_t *plain, size_t n)
{
    (void)u; char path[1200], hex[33]; uint8_t expected[80], saved[80]; size_t size=0;
    for(unsigned i=0;i<16;++i) snprintf(hex+2*i,3,"%02x",id[i]);
    snprintf(path,sizeof(path),"%s.effect-%s",base,hex);
    memcpy(expected,sender,32); memcpy(expected+32,id,16);
    if(falafel_crypto_sha256(plain,n,expected+48)) return -1;
    falafel_mail_posix store;
    if(falafel_mail_posix_open(&store,path)) return -1;
    falafel_mail_storage io=falafel_mail_posix_storage(&store);
    int r=io.load(io.user,saved,sizeof(saved),&size);
    if(r==1) r=io.replace(io.user,expected,sizeof(expected));
    else if(r==0 && (size!=sizeof(expected) || memcmp(saved,expected,sizeof(expected)))) r=-1;
    falafel_mail_posix_close(&store);
    if(!r && crash_effect) _exit(23);
    return r;
}
static bool received(void *u, const uint8_t *p, size_t n)
{
    (void)u; falafel_mail_frame frame;
    if(falafel_mail_frame_decode(p,n,&frame)) return false;
    falafel_mail_result r=falafel_mail_link_receive(&link_state,p,n);
    if(r==FALAFEL_MAIL_DUPLICATE) ++duplicates;
    else if(r==FALAFEL_MAIL_FULL) ++full;
    else if(r!=FALAFEL_MAIL_OK) ++rejected;
    return true;
}
static int send_frame(void *u, const uint8_t *p, size_t n)
{ return desktop_transport_send_frame(u,p,n); }
static int provision(const char *root)
{
    uint8_t priv[2][32],pub[2][32],file[64];
    for(unsigned i=0;i<2;++i)
        if(falafel_crypto_x25519_generate(&crypto,priv[i],32,pub[i],32)) return 2;
    for(unsigned i=0;i<2;++i) {
        char path[1200]; falafel_mail_posix store;
        snprintf(path,sizeof(path),"%s/%u.identity",root,i+1);
        if(falafel_mail_posix_open(&store,path)) return 2;
        falafel_mail_storage io=falafel_mail_posix_storage(&store); size_t n;
        if(io.load(io.user,file,64,&n)!=1) return 2; /* never rotate silently */
        memcpy(file,priv[i],32); memcpy(file+32,pub[1-i],32);
        if(io.replace(io.user,file,64)) return 2;
        falafel_mail_posix_close(&store);
        memset(&identity,0,sizeof(identity)); memcpy(identity.local_private,priv[i],32);
        memcpy(identity.local_public,pub[i],32); memcpy(identity.peer_public,pub[1-i],32);
        identity.has_pairing=1; identity.generation=1;
        snprintf(path,sizeof(path),"%s/%u.mail",root,i+1);
        if(falafel_mail_posix_open(&store,path)) return 2;
        if(falafel_mail_open(&mailbox,falafel_mail_posix_storage(&store),&crypto,&identity,1)) return 2;
        falafel_mail_posix_close(&store);
    }
    falafel_crypto_secure_zero(priv,sizeof(priv)); falafel_crypto_secure_zero(file,sizeof(file)); return 0;
}
int main(int argc,char **argv)
{
    if(argc<3 || falafel_crypto_init(&crypto)) return 2;
    if(!strcmp(argv[1],"provision")) { int r=provision(argv[2]); falafel_crypto_free(&crypto); return r; }
    /* run ROOT OWN PORT DURATION_MS MODE COUNT [TEXT]; fixture driver, not a
     * production identity provisioner. Credentials never go to relay or logs. */
    if(argc<8 || strcmp(argv[1],"run")) return 2;
    unsigned own=(unsigned)strtoul(argv[3],NULL,10), port=(unsigned)strtoul(argv[4],NULL,10);
    unsigned duration=(unsigned)strtoul(argv[5],NULL,10), count=(unsigned)strtoul(argv[7],NULL,10);
    if((own!=1 && own!=2) || !port || port>65535 || count>9) return 2;
    snprintf(base,sizeof(base),"%s/%u",argv[2],own);
    char path[1200]; uint8_t file[64]; size_t n; falafel_mail_posix store,credentials;
    snprintf(path,sizeof(path),"%s.identity",base);
    if(falafel_mail_posix_open(&credentials,path)) return 2;
    falafel_mail_storage io=falafel_mail_posix_storage(&credentials);
    if(io.load(io.user,file,sizeof(file),&n) || n!=64) return 2;
    falafel_mail_posix_close(&credentials);
    memcpy(identity.local_private,file,32); memcpy(identity.peer_public,file+32,32);
    falafel_crypto_secure_zero(file,sizeof(file)); identity.has_pairing=1; identity.generation=1;
    if(falafel_crypto_x25519_public_from_private(&crypto,identity.local_private,32,identity.local_public,32)) return 2;
    snprintf(path,sizeof(path),"%s.mail",base);
    if(falafel_mail_posix_open(&store,path) || falafel_mail_open(&mailbox,falafel_mail_posix_storage(&store),&crypto,&identity,0)) return 2;
    const char *text=argc>8?argv[8]:"FALAFEL_PRIVATE_PAYLOAD_3C2C";
    for(unsigned i=0;i<count;++i) {
        uint8_t id[16];
        if(falafel_crypto_random(&crypto,id,16)) return 2;
        falafel_mail_result r=falafel_mail_enqueue(&mailbox,id,(const uint8_t*)text,strlen(text));
        if(r==FALAFEL_MAIL_FULL) ++full; else if(r!=FALAFEL_MAIL_OK) return 2;
    }
    desktop_transport_init_frames(&transport,"127.0.0.1",(uint16_t)port,own,3-own,received,NULL);
    const char *chunk=getenv("FALAFEL_TEST_IO_CHUNK"); if(chunk) transport.io_chunk=(size_t)strtoul(chunk,NULL,10);
    falafel_mail_link_init(&link_state,&mailbox,own,3-own,send_frame,&transport);
    crash_effect=!strcmp(argv[6],"crash"); bool connected=false;
    uint64_t until=now_ms()+duration;
    while(now_ms()<until && mailbox.ready && !bad_effect) {
        desktop_transport_poll(&transport);
        bool current=desktop_transport_is_connected(&transport);
        if(current && !connected) falafel_mail_link_connected(&link_state);
        connected=current;
        if(current) falafel_mail_link_tick(&link_state,now_ms());
        if(strcmp(argv[6],"hold")) {
            falafel_mail_result r=falafel_mail_link_consume(&link_state,effect,NULL);
            if(r!=FALAFEL_MAIL_OK && r!=FALAFEL_MAIL_NOT_FOUND) bad_effect=1;
        }
        struct timespec pause={0,1000000}; nanosleep(&pause,NULL);
    }
    printf("{\"out\":["); for(unsigned i=0;i<8;++i) printf("%s%u",i?",":"",mailbox.outbox[i].state);
    printf("],\"in\":["); for(unsigned i=0;i<8;++i) printf("%s%u",i?",":"",mailbox.inbox[i].state);
    printf("],\"rejected\":%u,\"full\":%u,\"duplicates\":%u}\n",rejected,full,duplicates);
    desktop_transport_deinit(&transport); falafel_mail_posix_close(&store);
    falafel_crypto_secure_zero(&identity,sizeof(identity)); falafel_crypto_free(&crypto);
    return mailbox.ready && !bad_effect?0:2;
}
