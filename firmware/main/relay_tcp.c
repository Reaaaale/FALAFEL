#ifndef ESP_PLATFORM
#define _POSIX_C_SOURCE 200809L
#endif
#include "relay_tcp.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

static int64_t now_ms(void)
{
#ifdef ESP_PLATFORM
    return esp_timer_get_time() / 1000;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static int writable(int fd, int64_t deadline)
{
    for (;;) {
        int64_t now = now_ms();
        if (now < 0) return -1;
        int64_t remaining = deadline - now;
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        fd_set set;
        FD_ZERO(&set); FD_SET(fd, &set);
        struct timeval timeout = {.tv_sec = remaining / 1000,
                                  .tv_usec = (remaining % 1000) * 1000};
        int result = select(fd + 1, NULL, &set, NULL, &timeout);
        if (result > 0) return 0;
        if (!result) { errno = ETIMEDOUT; return -1; }
        if (errno != EINTR) return -1;
    }
}

int falafel_relay_tcp_open(const char *ipv4, uint16_t port,
                          uint32_t own, uint32_t peer)
{
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (!ipv4 || !port || !own || !peer || own == peer ||
        inet_pton(AF_INET, ipv4, &address.sin_addr) != 1) {
        errno = EINVAL; return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    if (fd >= FD_SETSIZE) { close(fd); errno = EMFILE; return -1; }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) goto failed;
    int keepalive = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive))) goto failed;
#ifdef SO_NOSIGPIPE
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &keepalive, sizeof(keepalive))) goto failed;
#endif
    int64_t start = now_ms();
    if (start < 0) goto failed;
    int64_t deadline = start + 5000;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) goto failed;
        if (writable(fd, deadline)) goto failed;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length)) goto failed;
        if (error) { errno = error; goto failed; }
    }
    char hello[64];
    int size = snprintf(hello, sizeof(hello), "HELLO2 %" PRIu32 " %" PRIu32 "\n", own, peer);
    if (size < 0 || (size_t)size >= sizeof(hello)) { errno = EINVAL; goto failed; }
    size_t offset = 0;
    while (offset < (size_t)size) {
        if (writable(fd, deadline)) goto failed;
        int send_flags = 0;
#ifdef MSG_NOSIGNAL
        send_flags = MSG_NOSIGNAL;
#endif
        ssize_t n = send(fd, hello + offset, (size_t)size - offset, send_flags);
        if (n > 0) offset += (size_t)n;
        else if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        else { if (!n) errno = EPIPE; goto failed; }
    }
    return fd;
failed:
    { int saved = errno; close(fd); errno = saved; }
    return -1;
}

void falafel_relay_io_init(falafel_relay_io *io, int fd, falafel_relay_receive receive, void *user)
{
    memset(io, 0, sizeof(*io)); io->fd = fd; io->receive = receive; io->user = user;
}

int falafel_relay_io_send(void *user, const uint8_t *wire, size_t size)
{
    falafel_relay_io *io = user;
    if (!io || io->fd < 0 || !wire || size < 30 || size > 286 || io->output_size) return 0;
    uint32_t prefix = htonl((uint32_t)size);
    memcpy(io->output, &prefix, 4); memcpy(io->output + 4, wire, size);
    io->output_offset = 0; io->output_size = size + 4;
    io->write_deadline = (uint64_t)now_ms() + 5000;
    return 1;
}

bool falafel_relay_io_poll(falafel_relay_io *io, uint64_t now)
{
    if (!io || io->fd < 0 || !io->receive) return false;
    if ((io->input_size && now >= io->read_deadline) ||
        (io->output_size && now >= io->write_deadline)) return false;
    if (io->output_size) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        size_t count = io->output_size - io->output_offset;
        if (io->io_chunk && count > io->io_chunk) count = io->io_chunk;
        ssize_t n = send(io->fd, io->output + io->output_offset, count, flags);
        if (n > 0) {
            io->output_offset += (size_t)n; io->write_deadline = now + 5000;
            if (io->output_offset == io->output_size) io->output_size = io->output_offset = 0;
        } else if (!n || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return false;
    }
    for (unsigned step = 0; step < 4; ++step) {
        size_t count = sizeof(io->input) - io->input_size;
        if (io->io_chunk && count > io->io_chunk) count = io->io_chunk;
        ssize_t n = recv(io->fd, io->input + io->input_size, count, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return true;
        if (n <= 0) return false;
        io->input_size += (size_t)n; io->read_deadline = now + 5000;
        while (io->input_size >= 4) {
            uint32_t prefix; memcpy(&prefix, io->input, 4); size_t size = ntohl(prefix);
            if (size < 30 || size > 286) return false;
            if (io->input_size < size + 4) break;
            if (!io->receive(io->user, io->input + 4, size)) return false;
            io->input_size -= size + 4;
            memmove(io->input, io->input + size + 4, io->input_size);
        }
    }
    return true;
}
