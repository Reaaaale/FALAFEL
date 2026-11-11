#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Transport preparation only: IPv4 TCP + existing HELLO2 registration.
 * Returns an owned nonblocking socket, or -1 with errno set. No DATA, ACK,
 * mailbox or crypto. A successful write does not prove relay acceptance.
 * Connect and registration share a five-second monotonic deadline. */
int falafel_relay_tcp_open(const char *ipv4, uint16_t port,
                          uint32_t own, uint32_t peer);

/* One bounded v2 TCP stream, caller owns/closes fd. Buffers include 4B prefix.
 * Queue success is volatile, never delivery. Clear all buffers on reconnect. */
typedef bool (*falafel_relay_receive)(void *, const uint8_t *, size_t);
typedef struct {
    int fd;
    uint8_t input[290], output[290];
    size_t input_size, output_size, output_offset;
    size_t io_chunk; /* zero in firmware; force partial I/O in host regressions */
    uint64_t read_deadline, write_deadline;
    falafel_relay_receive receive;
    void *user;
} falafel_relay_io;
void falafel_relay_io_init(falafel_relay_io *, int, falafel_relay_receive, void *);
int falafel_relay_io_send(void *, const uint8_t *, size_t);
bool falafel_relay_io_poll(falafel_relay_io *, uint64_t now_ms);
