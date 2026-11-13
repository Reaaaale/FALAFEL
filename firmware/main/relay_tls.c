#ifndef ESP_PLATFORM
#define _POSIX_C_SOURCE 200809L
#endif
#include "relay_tls.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static uint64_t clock_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static void pause_io(void) { vTaskDelay(1); }
#else
#include <time.h>
#include <unistd.h>
static uint64_t clock_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void pause_io(void) { struct timespec t = {.tv_nsec=1000000}; nanosleep(&t,NULL); }
#endif
falafel_tls *falafel_relay_tls_open(const char *ipv4, uint16_t port,
    const char *name, uint32_t own, uint32_t peer,
    const unsigned char *ca, size_t ca_n, const unsigned char *cert, size_t cert_n,
    const unsigned char *key, size_t key_n, int *fd)
{
    if (!fd) return NULL;
    *fd = -1;
    if (!own || !peer || own == peer) return NULL;
    uint64_t deadline = clock_ms() + 15000;
    *fd = falafel_relay_socket_open(ipv4, port);
    if (*fd < 0) return NULL;
    falafel_tls *tls = falafel_tls_create(*fd, name, ca, ca_n, cert, cert_n, key, key_n);
    if (!tls) goto failed;
    for (;;) {
        if (clock_ms() >= deadline) goto failed;
        int r = falafel_tls_handshake(tls);
        if (r < 0) goto failed;
        if (r > 0) break;
        pause_io();
    }
    char hello[64];
    int n = snprintf(hello, sizeof(hello), "HELLO2 %" PRIu32 " %" PRIu32 "\n", own, peer);
    if (n <= 0 || (size_t)n >= sizeof(hello)) goto failed;
    size_t offset = 0;
    while (offset < (size_t)n) {
        if (clock_ms() >= deadline) goto failed;
        ssize_t k = falafel_tls_write(tls, hello + offset, (size_t)n - offset);
        if (k > 0) offset += (size_t)k;
        else if (k < 0 && errno == EAGAIN) pause_io();
        else goto failed;
    }
    return tls;
failed:
    falafel_tls_free(tls);
    close(*fd); *fd = -1; errno = EACCES;
    return NULL;
}
void falafel_relay_tls_attach(falafel_relay_io *io, falafel_tls *tls)
{
    io->stream = tls;
    io->read = falafel_tls_read;
    io->write = falafel_tls_write;
}
