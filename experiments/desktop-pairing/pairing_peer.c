#include "falafel_pairing.h"
#include "pairing_io.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static int listen_once(const char *port)
{
    int server, client, yes = 1; struct sockaddr_in address = {0};
    server = socket(AF_INET, SOCK_STREAM, 0); if (server < 0) return -1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)strtoul(port, NULL, 10));
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) || listen(server, 1)) { close(server); return -1; }
    printf("listening on 127.0.0.1:%s\n", port); fflush(stdout);
    client = accept(server, NULL, NULL); close(server); return client;
}

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints = {0}, *addresses = NULL, *it; int fd = -1;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses)) return -1;
    for (it = addresses; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd >= 0 && !connect(fd, it->ai_addr, it->ai_addrlen)) break;
        if (fd >= 0) close(fd); fd = -1;
    }
    freeaddrinfo(addresses); return fd;
}

int main(int argc, char **argv)
{
    falafel_pairing_role role; falafel_pairing *p = NULL; int fd; unsigned flight;
    uint8_t key[32], message[256]; size_t size; char sas[13], answer[16];
    if (argc == 3 && !strcmp(argv[1], "listen")) {
        role = FALAFEL_PAIRING_RESPONDER; fd = listen_once(argv[2]);
    } else if (argc == 4 && !strcmp(argv[1], "connect")) {
        role = FALAFEL_PAIRING_INITIATOR; fd = connect_to(argv[2], argv[3]);
    } else {
        fprintf(stderr, "usage: %s listen PORT | connect HOST PORT\n", argv[0]); return 2;
    }
    if (fd < 0 || falafel_pairing_generate_identity(key) || falafel_pairing_new(&p, role, key)) return 1;
    memset(key, 0, sizeof(key));
    for (flight = 0; flight < 3; ++flight) {
        int sends = (flight != 1) == (role == FALAFEL_PAIRING_INITIATOR);
        if ((sends && (falafel_pairing_write_handshake(p, message, sizeof(message), &size) || send_frame(fd, message, size))) ||
            (!sends && (recv_frame(fd, message, sizeof(message), &size) || falafel_pairing_read_handshake(p, message, size)))) goto error;
    }
    if (falafel_pairing_get_sas(p, sas)) goto error;
    printf("Compare SAS: %s\nConfirm only if both displays match [yes/no]: ", sas); fflush(stdout);
    if (!fgets(answer, sizeof(answer), stdin)) goto error;
    if (!strncmp(answer, "yes", 3)) {
        if (falafel_pairing_confirm(p, sas, message, sizeof(message), &size)) goto error;
    } else if (falafel_pairing_reject(p, message, sizeof(message), &size)) goto error;
    if (send_frame(fd, message, size) || recv_frame(fd, message, sizeof(message), &size) ||
        falafel_pairing_read_confirmation(p, message, size)) goto error;
    close(fd);
    if (falafel_pairing_get_state(p) == FALAFEL_PAIRING_COMPLETE) { puts("pairing completed"); falafel_pairing_free(p); return 0; }
    puts("pairing rejected"); falafel_pairing_free(p); return 3;
error:
    fprintf(stderr, "pairing error\n"); close(fd); falafel_pairing_free(p); return 1;
}
