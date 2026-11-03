#include "falafel_e2ee.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../shared/e2ee/falafel_e2ee.c"
static void identities(falafel_pairing_persistent_state *a, falafel_pairing_persistent_state *b)
{
    uint8_t x[32], y[32]; assert(!falafel_pairing_generate_identity(x)); assert(!falafel_pairing_generate_identity(y));
    assert(!falafel_pairing_persistent_init(a, x)); assert(!falafel_pairing_persistent_init(b, y));
    a->has_pairing = b->has_pairing = 1; memcpy(a->peer_public, b->local_public, 32); memcpy(b->peer_public, a->local_public, 32);
}
static void establish(falafel_e2ee **a, falafel_e2ee **b, falafel_pairing_persistent_state *x, falafel_pairing_persistent_state *y, uint8_t *first, size_t *first_n)
{
    uint8_t m[256]; size_t n;
    assert(!falafel_e2ee_new(a, FALAFEL_PAIRING_INITIATOR, x)); assert(!falafel_e2ee_new(b, FALAFEL_PAIRING_RESPONDER, y));
    assert(!falafel_e2ee_write_handshake(*a, m, sizeof(m), &n)); if (first) { memcpy(first, m, n); *first_n = n; }
    assert(!falafel_e2ee_read_handshake(*b, m, n)); assert(!falafel_e2ee_write_handshake(*b, m, sizeof(m), &n));
    assert(!falafel_e2ee_read_handshake(*a, m, n)); assert(falafel_e2ee_status(*a) == FALAFEL_E2EE_READY);
}
static void exhaustion(falafel_pairing_persistent_state *x, falafel_pairing_persistent_state *y)
{
    falafel_e2ee *a, *b; uint8_t wire[64], output[64], previous[64]; size_t n, len;
    establish(&a, &b, x, y, NULL, NULL);
    assert(!noise_cipherstate_set_nonce(a->send, UINT64_MAX - 1));
    assert(!noise_cipherstate_set_nonce(b->receive, UINT64_MAX - 1));
    a->send_seq = b->recv_seq = UINT64_MAX - 1;
    assert(!falafel_e2ee_encrypt(a, (const uint8_t *)"x", 1, wire, sizeof(wire), &n));
    assert(n == 25 && wire[7] == 0xfe);
    memcpy(previous, wire, n);
    assert(!falafel_e2ee_decrypt(b, wire, n, output, sizeof(output), &len));
    assert(len == 1 && output[0] == 'x');
    /* Noise rejects nonce rewind, and both wrapper counters have reached the
       reserved UINT64_MAX value: neither operation can reuse a nonce. */
    assert(noise_cipherstate_set_nonce(a->send, UINT64_MAX - 1) == NOISE_ERROR_INVALID_NONCE);
    assert(falafel_e2ee_encrypt(a, (const uint8_t *)"x", 1, wire, sizeof(wire), &n));
    assert(falafel_e2ee_status(a) == FALAFEL_E2EE_CLOSED);
    assert(falafel_e2ee_encrypt(a, (const uint8_t *)"x", 1, wire, sizeof(wire), &n));
    assert(falafel_e2ee_decrypt(b, previous, 25, output, sizeof(output), &len));
    assert(falafel_e2ee_status(b) == FALAFEL_E2EE_CLOSED);
    falafel_e2ee_free(a); falafel_e2ee_free(b);
}
int main(void)
{
    falafel_pairing_persistent_state x, y; falafel_e2ee *a, *b; uint8_t m[256], saved[256], out[256], first[256]; size_t n, k, first_n;
    identities(&x, &y); exhaustion(&x, &y); establish(&a, &b, &x, &y, first, &first_n);
    assert(!falafel_e2ee_encrypt(a, (const uint8_t *)"hello", 5, m, sizeof(m), &n)); memcpy(saved, m, n);
    assert(!falafel_e2ee_decrypt(b, m, n, out, sizeof(out), &k)); assert(k == 5 && !memcmp(out, "hello", 5));
    assert(!falafel_e2ee_encrypt(b, (const uint8_t *)"world", 5, m, sizeof(m), &n));
    assert(!falafel_e2ee_decrypt(a, m, n, out, sizeof(out), &k)); assert(k == 5 && !memcmp(out, "world", 5));
    assert(falafel_e2ee_decrypt(b, saved, 29, out, sizeof(out), &k)); assert(falafel_e2ee_status(b) == FALAFEL_E2EE_CLOSED);
    falafel_e2ee_free(a); falafel_e2ee_free(b);
    establish(&a, &b, &x, &y, NULL, NULL);
    assert(!falafel_e2ee_encrypt(a, (const uint8_t *)"one", 3, m, sizeof(m), &n));
    assert(!falafel_e2ee_encrypt(a, (const uint8_t *)"two", 3, m, sizeof(m), &n));
    assert(m[7] == 1); assert(falafel_e2ee_decrypt(b, m, n, out, sizeof(out), &k));
    assert(falafel_e2ee_status(b) == FALAFEL_E2EE_CLOSED); falafel_e2ee_free(a); falafel_e2ee_free(b);
    establish(&a, &b, &x, &y, NULL, NULL);
    assert(!falafel_e2ee_encrypt(a, (const uint8_t *)"hello", 5, m, sizeof(m), &n)); assert(memcmp(m + 8, saved + 8, n - 8));
    m[n - 1] ^= 1; assert(falafel_e2ee_decrypt(b, m, n, out, sizeof(out), &k));
    falafel_e2ee_close(a); assert(falafel_e2ee_encrypt(a, (const uint8_t *)"x", 1, m, sizeof(m), &n));
    falafel_e2ee_free(a); falafel_e2ee_free(b);
    y.peer_public[0] ^= 1; assert(!falafel_e2ee_new(&a, FALAFEL_PAIRING_INITIATOR, &x)); assert(!falafel_e2ee_new(&b, FALAFEL_PAIRING_RESPONDER, &y));
    assert(!falafel_e2ee_write_handshake(a, m, sizeof(m), &n)); assert(n == first_n && memcmp(m, first, n));
    assert(falafel_e2ee_read_handshake(b, m, n)); assert(falafel_e2ee_status(b) == FALAFEL_E2EE_CLOSED);
    falafel_e2ee_free(a); falafel_e2ee_free(b); falafel_pairing_persistent_clear(&x); falafel_pairing_persistent_clear(&y);
    puts("E2EE tests: PASS"); return 0;
}
