#define _POSIX_C_SOURCE 200809L
#include "desktop_transport.h"
#include "relay_tcp.h"
#include "falafel_tls.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pause_io(void) { struct timespec t={.tv_nsec=1000000};nanosleep(&t,NULL); }
static bool receive_frame(void *u,const uint8_t *b,size_t n) { (void)u;(void)b;(void)n;return true; }
int main(int argc,char **argv)
{
    if(argc!=8) return 2;
    uint16_t port=(uint16_t)strtoul(argv[3],NULL,10);
    uint64_t deadline=now()+2000;
    if(!strcmp(argv[1],"desktop")) {
        desktop_transport_t transport;
        desktop_transport_init_frames(&transport,argv[2],port,1,2,receive_frame,NULL);
        if(!desktop_transport_configure_tls(&transport,argv[4],argv[5],argv[6],argv[7])) {
            desktop_transport_deinit(&transport);return 1;
        }
        bool authenticated=false;
        while(now()<deadline && !authenticated) {
            desktop_transport_poll(&transport);
            authenticated=desktop_transport_is_connected(&transport);pause_io();
        }
        desktop_transport_deinit(&transport);
        if(authenticated) puts("AUTHENTICATED");
        return authenticated?0:1;
    }
    int fd=falafel_relay_socket_open(argv[2],port);
    if(fd<0) return 1;
    falafel_tls *tls=falafel_tls_files(fd,argv[4],argv[5],argv[6],argv[7]);
    int result=0;
    while(now()<deadline && result==0) { result=falafel_tls_handshake(tls);pause_io(); }
    falafel_tls_free(tls);close(fd);
    if(result==1) puts("AUTHENTICATED");
    return result==1?0:1;
}
