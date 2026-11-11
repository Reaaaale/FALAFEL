#define _POSIX_C_SOURCE 200809L
#include "relay_tcp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    int fd = falafel_relay_tcp_open(argv[1], (uint16_t)strtoul(argv[2], NULL, 10),
                                  (uint32_t)strtoul(argv[3], NULL, 10),
                                  (uint32_t)strtoul(argv[4], NULL, 10));
    if (fd < 0) { puts("OPEN_FAILED"); return 1; }
    puts("HELLO2_WRITTEN"); fflush(stdout);
    /* One-second observation; discard bytes without interpreting or ACKing. */
    for (unsigned i = 0; i < 20; ++i) {
        fd_set readset; FD_ZERO(&readset); FD_SET(fd, &readset);
        struct timeval timeout = {.tv_usec = 50000};
        int ready = select(fd + 1, &readset, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) { close(fd); return 2; }
        if (!ready) continue;
        unsigned char discard[128];
        ssize_t n = recv(fd, discard, sizeof(discard), 0);
        if (!n || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            puts("CLOSED"); close(fd); return 0;
        }
    }
    puts("IDLE"); close(fd); return 0;
}
