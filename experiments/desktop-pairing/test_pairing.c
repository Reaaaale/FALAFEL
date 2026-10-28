#include "falafel_pairing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { falafel_pairing *i, *r; uint8_t m[256]; size_t n; char sas_i[13], sas_r[13]; } pair;

static void destroy(pair *p) { falafel_pairing_free(p->i); falafel_pairing_free(p->r); memset(p, 0, sizeof(*p)); }
static void ready(pair *p)
{
    uint8_t ik[32], rk[32]; memset(p, 0, sizeof(*p));
    assert(!falafel_pairing_generate_identity(ik)); assert(!falafel_pairing_generate_identity(rk)); assert(memcmp(ik, rk, 32));
    assert(!falafel_pairing_new(&p->i, FALAFEL_PAIRING_INITIATOR, ik));
    assert(!falafel_pairing_new(&p->r, FALAFEL_PAIRING_RESPONDER, rk));
    assert(falafel_pairing_get_state(p->i) == FALAFEL_PAIRING_INITIAL);
    assert(falafel_pairing_get_state(p->r) == FALAFEL_PAIRING_INITIAL);
    assert(!falafel_pairing_write_handshake(p->i, p->m, sizeof(p->m), &p->n));
    assert(!falafel_pairing_read_handshake(p->r, p->m, p->n));
    assert(!falafel_pairing_write_handshake(p->r, p->m, sizeof(p->m), &p->n));
    assert(!falafel_pairing_read_handshake(p->i, p->m, p->n));
    assert(!falafel_pairing_write_handshake(p->i, p->m, sizeof(p->m), &p->n));
    assert(!falafel_pairing_read_handshake(p->r, p->m, p->n));
    assert(!falafel_pairing_get_sas(p->i, p->sas_i)); assert(!falafel_pairing_get_sas(p->r, p->sas_r));
    assert(!strcmp(p->sas_i, p->sas_r)); assert(strlen(p->sas_i) == 12);
}
static void positive(void)
{
    pair p; uint8_t other[256]; size_t other_n;
    ready(&p);
    assert(!falafel_pairing_confirm(p.i, p.sas_i, p.m, sizeof(p.m), &p.n));
    assert(falafel_pairing_get_state(p.i) == FALAFEL_PAIRING_LOCAL_CONFIRMED);
    assert(!falafel_pairing_confirm(p.r, p.sas_r, other, sizeof(other), &other_n));
    assert(!falafel_pairing_read_confirmation(p.i, other, other_n));
    assert(!falafel_pairing_read_confirmation(p.r, p.m, p.n));
    assert(falafel_pairing_get_state(p.i) == FALAFEL_PAIRING_COMPLETE);
    assert(falafel_pairing_get_state(p.r) == FALAFEL_PAIRING_COMPLETE);
    assert(falafel_pairing_read_confirmation(p.i, other, other_n)); /* completed session reuse */
    destroy(&p);
}
static void sas_mismatch(void)
{
    pair p; ready(&p); p.sas_i[0] = p.sas_i[0] == '9' ? '0' : (char)(p.sas_i[0] + 1);
    assert(!falafel_pairing_confirm(p.i, p.sas_i, p.m, sizeof(p.m), &p.n));
    assert(falafel_pairing_get_state(p.i) == FALAFEL_PAIRING_REJECTED);
    assert(!falafel_pairing_read_confirmation(p.r, p.m, p.n));
    assert(falafel_pairing_get_state(p.r) == FALAFEL_PAIRING_REJECTED); destroy(&p);
}
static void rejection(void)
{
    pair p; uint8_t peer[32]; ready(&p); assert(!falafel_pairing_reject(p.r, p.m, sizeof(p.m), &p.n));
    assert(!falafel_pairing_read_confirmation(p.i, p.m, p.n));
    assert(falafel_pairing_get_state(p.i) == FALAFEL_PAIRING_REJECTED);
    assert(falafel_pairing_get_peer_public_key(p.i, peer));
    assert(falafel_pairing_get_peer_public_key(p.r, peer)); destroy(&p);
}
static void altered(void)
{
    pair p; ready(&p); assert(!falafel_pairing_confirm(p.i, p.sas_i, p.m, sizeof(p.m), &p.n));
    p.m[p.n - 1] ^= 1; assert(falafel_pairing_read_confirmation(p.r, p.m, p.n));
    assert(falafel_pairing_get_state(p.r) == FALAFEL_PAIRING_ERROR); destroy(&p);
}
static void missing_and_sequence(void)
{
    pair p; uint8_t key[32], data[256]; size_t n;
    ready(&p); assert(!falafel_pairing_confirm(p.i, p.sas_i, p.m, sizeof(p.m), &p.n));
    assert(falafel_pairing_get_state(p.i) == FALAFEL_PAIRING_LOCAL_CONFIRMED);
    assert(falafel_pairing_get_state(p.r) == FALAFEL_PAIRING_WAIT_SAS); destroy(&p);
    assert(!falafel_pairing_generate_identity(key)); assert(!falafel_pairing_new(&p.r, FALAFEL_PAIRING_RESPONDER, key));
    assert(falafel_pairing_confirm(p.r, "000000000000", data, sizeof(data), &n));
    assert(falafel_pairing_get_state(p.r) == FALAFEL_PAIRING_ERROR); destroy(&p);
}
static void altered_handshake(void)
{
    pair p; uint8_t ik[32], rk[32]; memset(&p, 0, sizeof(p));
    assert(!falafel_pairing_generate_identity(ik)); assert(!falafel_pairing_generate_identity(rk));
    assert(!falafel_pairing_new(&p.i, FALAFEL_PAIRING_INITIATOR, ik)); assert(!falafel_pairing_new(&p.r, FALAFEL_PAIRING_RESPONDER, rk));
    assert(!falafel_pairing_write_handshake(p.i, p.m, sizeof(p.m), &p.n)); assert(!falafel_pairing_read_handshake(p.r, p.m, p.n));
    assert(!falafel_pairing_write_handshake(p.r, p.m, sizeof(p.m), &p.n)); p.m[p.n - 1] ^= 1;
    assert(falafel_pairing_read_handshake(p.i, p.m, p.n)); destroy(&p);
}
int main(void) { positive(); sas_mismatch(); rejection(); altered(); missing_and_sequence(); altered_handshake(); puts("pairing tests: PASS"); return 0; }
