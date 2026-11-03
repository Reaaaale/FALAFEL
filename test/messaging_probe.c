#define _POSIX_C_SOURCE 200809L
#include "messaging_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stopped;
static void stop(int s) { (void)s; stopped = 1; }
static uint64_t now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
int main(int argc, char **argv)
{
    assert(!messaging_valid_text("", 0));
    assert(!messaging_valid_text("a\0b", 3));
    assert(!messaging_valid_text("\xc0\x80", 2));
    assert(!messaging_valid_text("\xed\xa0\x80", 3));
    assert(!messaging_valid_text("\xf4\x90\x80\x80", 4));
    assert(messaging_valid_text("Caff\xc3\xa8", 6));
    if (argc != 8) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0); signal(SIGTERM, stop); signal(SIGINT, stop);
    messaging_config config = {.own = (uint32_t)atoi(argv[1]), .peer = (uint32_t)atoi(argv[2]),
        .port = (uint16_t)atoi(argv[3]), .host = "127.0.0.1", .identity_path = argv[4], .mailbox_path = argv[5],.create_history=true,.history_path=getenv("FALAFEL_PROBE_HISTORY")};
    messaging_controller *c = messaging_start(&config);
    if (!c) return 2;
    bool ready = false, fatal = false, sent = false;
    bool flood = !strcmp(argv[7], "flood"), stop_full = !strcmp(argv[7], "stop-full");
    uint64_t begin = now(), ready_at = 0;
    unsigned accepted = 0, rejected = 0;
    const char *delay_text = getenv("FALAFEL_PROBE_SEND_DELAY_MS");
    unsigned delay = delay_text ? (unsigned)atoi(delay_text) : 0;
    bool closing = false;
    while (!messaging_finished(c)) {
        uint64_t elapsed = now() - begin;
        if (stopped || elapsed > (unsigned)atoi(argv[6]) || fatal) closing = true;
        if (!closing && ready && now() - ready_at >= delay &&
            ((!flood && !stop_full && !sent && strcmp(argv[7], "hold")) ||
             ((flood || stop_full) && now() - ready_at < 700))) {
            uint64_t request;
            const char *text = flood || stop_full ? "flood" : argv[7];
            if (messaging_try_send(c, text, strlen(text), &request)) { ++accepted; sent = true; }
            else ++rejected;
        }
        /* Deliberately saturate worker->GUI, then drain or stop while full. */
        if (!ready || (!stop_full && (!flood || now() - ready_at >= 700))) {
            messaging_event e;
            while (messaging_try_event(c, &e)) {
                if (e.kind == MESSAGING_READY) { ready = true; ready_at = now(); }
                fatal = fatal || e.fatal;
                printf("EVENT %u request=%llu error=%u fatal=%u restored=%u id=", e.kind,
                    (unsigned long long)e.request, e.error, e.fatal, e.restored);
                for (unsigned i = 0; i < 16; ++i) printf("%02x", e.id[i]);
                printf(" text_match=%u\n", !strcmp(e.text, argv[7]));
                memset(&e, 0, sizeof(e));
                if (flood || stop_full) break;
            }
        }
        if (closing) messaging_request_stop(c);
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
        if (elapsed > (unsigned)atoi(argv[6]) + 5000) return 3;
    }
    /* Drain final results after worker termination. */
    messaging_event e;
    while (messaging_try_event(c, &e)) {
        fatal = fatal || e.fatal;
        printf("EVENT %u request=%llu error=%u fatal=%u restored=%u\n", e.kind,
            (unsigned long long)e.request, e.error, e.fatal, e.restored);
    }
    fatal = fatal || messaging_failed(c);
    printf("SUMMARY accepted=%u rejected=%u fatal=%u\n", accepted, rejected, fatal);
    messaging_destroy(c); return fatal ? 1 : 0;
}
