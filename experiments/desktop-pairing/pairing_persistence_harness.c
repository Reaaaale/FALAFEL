#include "falafel_pairing.h"
#include "falafel_e2ee.h"
#include "pairing_io.h"
#include "pairing_store_macos.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { char path[256], service[128], account[64]; falafel_pairing_macos_store store; } location;
static void location_init(location *l, const char *directory, const char *name)
{
    snprintf(l->path, sizeof(l->path), "%s/%s.state", directory, name);
    snprintf(l->service, sizeof(l->service), "org.falafel.pairing.test.%ld", (long)getppid());
    snprintf(l->account, sizeof(l->account), "%s", name);
    l->store.metadata_path = l->path; l->store.keychain_service = l->service; l->store.keychain_account = l->account;
}
static int pair_peer(int fd, falafel_pairing_role role, location *location)
{
    falafel_pairing_persistent_state state; falafel_pairing *pairing = NULL;
    uint8_t wire[256]; size_t size; char sas[13];
    if (falafel_pairing_macos_load_or_create(&location->store, &state) ||
        falafel_pairing_new(&pairing, role, state.local_private)) return 1;
    for (unsigned flight = 0; flight < 3; ++flight) {
        int sends = (flight != 1) == (role == FALAFEL_PAIRING_INITIATOR);
        if ((sends && (falafel_pairing_write_handshake(pairing, wire, sizeof(wire), &size) || send_frame(fd, wire, size))) ||
            (!sends && (recv_frame(fd, wire, sizeof(wire), &size) || falafel_pairing_read_handshake(pairing, wire, size)))) return 2;
    }
    if (falafel_pairing_get_sas(pairing, sas) || falafel_pairing_confirm(pairing, sas, wire, sizeof(wire), &size) ||
        send_frame(fd, wire, size) || recv_frame(fd, wire, sizeof(wire), &size) ||
        falafel_pairing_read_confirmation(pairing, wire, size) ||
        falafel_pairing_persistent_complete(&state, pairing) || falafel_pairing_macos_save(&location->store, &state)) return 3;
    falafel_pairing_free(pairing); falafel_pairing_persistent_clear(&state); return 0;
}
static int reload_peer(location *self, location *other)
{
    falafel_pairing_persistent_state a, b; int rc = 1;
    if (!falafel_pairing_macos_load_or_create(&self->store, &a) &&
        !falafel_pairing_macos_load_or_create(&other->store, &b) && a.has_pairing && b.has_pairing &&
        !memcmp(a.peer_public, b.local_public, 32)) rc = 0;
    falafel_pairing_persistent_clear(&a); falafel_pairing_persistent_clear(&b); return rc;
}
static int secure_peer(int fd, falafel_pairing_role role, location *location)
{
    falafel_pairing_persistent_state state; falafel_e2ee *s = NULL; uint8_t wire[256], plain[256]; size_t n, len;
    if (falafel_pairing_macos_load_or_create(&location->store, &state) ||
        falafel_e2ee_new(&s, role, &state)) return 1;
    if (role == FALAFEL_PAIRING_INITIATOR) {
        if (falafel_e2ee_write_handshake(s, wire, sizeof(wire), &n) || send_frame(fd, wire, n) ||
            recv_frame(fd, wire, sizeof(wire), &n) || falafel_e2ee_read_handshake(s, wire, n) ||
            falafel_e2ee_encrypt(s, (const uint8_t *)"ping", 4, wire, sizeof(wire), &n) || send_frame(fd, wire, n) ||
            recv_frame(fd, wire, sizeof(wire), &n) || falafel_e2ee_decrypt(s, wire, n, plain, sizeof(plain), &len) ||
            len != 4 || memcmp(plain, "pong", 4)) return 2;
    } else {
        if (recv_frame(fd, wire, sizeof(wire), &n) || falafel_e2ee_read_handshake(s, wire, n) ||
            falafel_e2ee_write_handshake(s, wire, sizeof(wire), &n) || send_frame(fd, wire, n) ||
            recv_frame(fd, wire, sizeof(wire), &n) || falafel_e2ee_decrypt(s, wire, n, plain, sizeof(plain), &len) ||
            len != 4 || memcmp(plain, "ping", 4) ||
            falafel_e2ee_encrypt(s, (const uint8_t *)"pong", 4, wire, sizeof(wire), &n) || send_frame(fd, wire, n)) return 3;
    }
    falafel_e2ee_free(s); falafel_pairing_persistent_clear(&state); return 0;
}
static int wrong_identity_peer(int fd, int attacker, location *location)
{
    falafel_pairing_persistent_state state, rogue; falafel_e2ee *session = NULL;
    uint8_t key[32], wire[256], reply = 0; size_t n; int rc = 1;
    memset(&state, 0, sizeof(state)); memset(&rogue, 0, sizeof(rogue));
    if (falafel_pairing_macos_load_or_create(&location->store, &state) || !state.has_pairing) goto done;
    if (attacker) {
        if (falafel_pairing_generate_identity(key) || falafel_pairing_persistent_init(&rogue, key)) goto done;
        memset(key, 0, sizeof(key));
        if (!memcmp(rogue.local_public, state.local_public, 32)) goto done;
        memcpy(rogue.peer_public, state.peer_public, 32); rogue.has_pairing = 1;
        if (falafel_e2ee_new(&session, FALAFEL_PAIRING_INITIATOR, &rogue) ||
            falafel_e2ee_write_handshake(session, wire, sizeof(wire), &n) || send_frame(fd, wire, n) ||
            io_all(fd, &reply, 1, 0) || reply != 1) goto done;
        rc = 0;
    } else {
        if (falafel_e2ee_new(&session, FALAFEL_PAIRING_RESPONDER, &state) ||
            recv_frame(fd, wire, sizeof(wire), &n)) goto done;
        if (!falafel_e2ee_read_handshake(session, wire, n) ||
            falafel_e2ee_status(session) != FALAFEL_E2EE_CLOSED) goto done;
        /* A rejected peer must not yield a usable session or accept data. */
        {
            uint8_t output[256]; size_t output_size;
            if (!falafel_e2ee_decrypt(session, wire, n, output, sizeof(output), &output_size) ||
                falafel_e2ee_status(session) != FALAFEL_E2EE_CLOSED) goto done;
        }
        reply = 1; if (io_all(fd, &reply, 1, 1)) goto done;
        rc = 0;
    }
done:
    memset(key, 0, sizeof(key)); falafel_e2ee_free(session);
    falafel_pairing_persistent_clear(&state); falafel_pairing_persistent_clear(&rogue); return rc;
}
int main(void)
{
    char directory[] = "/tmp/falafel-persist-XXXXXX"; location a, b; int sockets[2], sa, sb; pid_t pa, pb;
    falafel_pairing_persistent_state before, after; falafel_pairing *incomplete = NULL;
    uint8_t old_public[32], wire[256]; size_t wire_size; FILE *file;
    if (!mkdtemp(directory)) return 1; location_init(&a, directory, "a"); location_init(&b, directory, "b");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 3;
    pa = fork(); if (!pa) { close(sockets[1]); exit(pair_peer(sockets[0], FALAFEL_PAIRING_INITIATOR, &a)); }
    pb = fork(); if (!pb) { close(sockets[0]); exit(pair_peer(sockets[1], FALAFEL_PAIRING_RESPONDER, &b)); }
    close(sockets[0]); close(sockets[1]); waitpid(pa, &sa, 0); waitpid(pb, &sb, 0);
    if (!WIFEXITED(sa) || WEXITSTATUS(sa) || !WIFEXITED(sb) || WEXITSTATUS(sb)) {
        fprintf(stderr, "pairing children failed: initiator=%d responder=%d\n",
                WIFEXITED(sa) ? WEXITSTATUS(sa) : -1, WIFEXITED(sb) ? WEXITSTATUS(sb) : -1);
        return 4;
    }
    pa = fork(); if (!pa) exit(reload_peer(&a, &b)); waitpid(pa, &sa, 0);
    pb = fork(); if (!pb) exit(reload_peer(&b, &a)); waitpid(pb, &sb, 0);
    if (WEXITSTATUS(sa) || WEXITSTATUS(sb)) return 5;
    for (unsigned round = 0; round < 2; ++round) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 12;
        pa = fork(); if (!pa) { close(sockets[1]); exit(secure_peer(sockets[0], FALAFEL_PAIRING_INITIATOR, &a)); }
        pb = fork(); if (!pb) { close(sockets[0]); exit(secure_peer(sockets[1], FALAFEL_PAIRING_RESPONDER, &b)); }
        close(sockets[0]); close(sockets[1]); waitpid(pa, &sa, 0); waitpid(pb, &sb, 0);
        if (!WIFEXITED(sa) || WEXITSTATUS(sa) || !WIFEXITED(sb) || WEXITSTATUS(sb)) return 13;
    }
    /* A third identity can address B, but B must reject it against A's
       previously persisted identity before sending handshake flight two. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 14;
    pa = fork(); if (!pa) { close(sockets[1]); exit(wrong_identity_peer(sockets[0], 1, &a)); }
    pb = fork(); if (!pb) { close(sockets[0]); exit(wrong_identity_peer(sockets[1], 0, &b)); }
    close(sockets[0]); close(sockets[1]); waitpid(pa, &sa, 0); waitpid(pb, &sb, 0);
    if (!WIFEXITED(sa) || WEXITSTATUS(sa) || !WIFEXITED(sb) || WEXITSTATUS(sb)) return 15;
    /* An interrupted replacement leaves the last committed pairing intact. */
    if (falafel_pairing_macos_load_or_create(&a.store, &before)) return 6;
    if (falafel_pairing_macos_simulate_interrupted_save(&a.store, &before) != FALAFEL_STORE_ERROR ||
        falafel_pairing_macos_load_or_create(&a.store, &after) || !after.has_pairing ||
        memcmp(before.peer_public, after.peer_public, 32)) return 7;
    /* A corrupt committed file fails closed and never restores a pairing. */
    file = fopen(a.path, "r+b"); if (!file || fputc('X', file) == EOF || fclose(file) ||
        falafel_pairing_macos_load_or_create(&a.store, &after) != FALAFEL_STORE_CORRUPT) return 8;
    /* Explicit unpair recovers, removes peer, and rotates identity. */
    memcpy(old_public, before.local_public, 32);
    if (falafel_pairing_macos_unpair(&a.store, 1, &after) || after.has_pairing ||
        !memcmp(old_public, after.local_public, 32)) return 9;
    /* Restart during an uncommitted pairing retains identity but no peer. */
    if (falafel_pairing_macos_unpair(&b.store, 0, &after) ||
        falafel_pairing_new(&incomplete, FALAFEL_PAIRING_INITIATOR, after.local_private) ||
        falafel_pairing_write_handshake(incomplete, wire, sizeof(wire), &wire_size)) return 10;
    falafel_pairing_free(incomplete); incomplete = NULL;
    if (falafel_pairing_macos_load_or_create(&b.store, &before) || before.has_pairing ||
        memcmp(before.local_public, after.local_public, 32)) return 11;
    falafel_pairing_macos_destroy(&a.store); falafel_pairing_macos_destroy(&b.store);
    rmdir(directory);
    puts("persistent pairing restart/unpair: PASS"); return 0;
}
