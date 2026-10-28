#include "falafel_pairing.h"
#include "pairing_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>

static int peer(int fd, falafel_pairing_role role)
{
    uint8_t key[32], message[256]; size_t size; char sas[13]; falafel_pairing *p = NULL;
    if (falafel_pairing_generate_identity(key) || falafel_pairing_new(&p, role, key)) return 1;
    for (unsigned flight = 0; flight < 3; ++flight) {
        int sends = (flight != 1) == (role == FALAFEL_PAIRING_INITIATOR);
        if ((sends && (falafel_pairing_write_handshake(p, message, sizeof(message), &size) || send_frame(fd, message, size))) ||
            (!sends && (recv_frame(fd, message, sizeof(message), &size) || falafel_pairing_read_handshake(p, message, size)))) return 2;
    }
    if (falafel_pairing_get_sas(p, sas)) return 3;
    printf("%s SAS: %s\n", role == FALAFEL_PAIRING_INITIATOR ? "initiator" : "responder", sas);
    if (falafel_pairing_confirm(p, sas, message, sizeof(message), &size) || send_frame(fd, message, size) ||
        recv_frame(fd, message, sizeof(message), &size) || falafel_pairing_read_confirmation(p, message, size)) return 4;
    if (falafel_pairing_get_state(p) != FALAFEL_PAIRING_COMPLETE) return 5;
    falafel_pairing_free(p); return 0;
}

int main(void)
{
    int sockets[2], status1, status2; pid_t a, b;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 1;
    a = fork(); if (a == 0) { close(sockets[1]); exit(peer(sockets[0], FALAFEL_PAIRING_INITIATOR)); }
    b = fork(); if (b == 0) { close(sockets[0]); exit(peer(sockets[1], FALAFEL_PAIRING_RESPONDER)); }
    close(sockets[0]); close(sockets[1]);
    if (a < 0 || b < 0 || waitpid(a, &status1, 0) < 0 || waitpid(b, &status2, 0) < 0 ||
        !WIFEXITED(status1) || WEXITSTATUS(status1) || !WIFEXITED(status2) || WEXITSTATUS(status2)) return 1;
    puts("two-process pairing: PASS"); return 0;
}
