#include "falafel_async_e2ee.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static falafel_crypto_context_t crypto;
static void identity(falafel_pairing_persistent_state *a)
{
    memset(a, 0, sizeof(*a)); a->has_pairing = 1; a->generation = 1;
    assert(!falafel_crypto_x25519_generate(&crypto, a->local_private, 32, a->local_public, 32));
}
int main(void)
{
    falafel_pairing_persistent_state a, b, stranger, bad;
    uint8_t wire[256], again[256], altered[257], out[123], plain[123], mid[16] = {1}, got[16];
    size_t n, m, k;
    assert(!falafel_crypto_init(&crypto)); identity(&a); identity(&b); identity(&stranger);
    memcpy(a.peer_public, b.local_public, 32); memcpy(b.peer_public, a.local_public, 32);
    memcpy(stranger.peer_public, a.local_public, 32); memset(plain, 0x6a, sizeof(plain));
    for (size_t len = 0; len <= 122; ++len) {
        assert(!falafel_async_encrypt(&crypto, &a, mid, len ? plain : NULL, len, wire, sizeof(wire), &n));
        assert(n == len + 134);
        assert(!falafel_async_decrypt(&crypto, &b, wire, n, out, len, &m, got));
        assert(m == len && !memcmp(out, plain, len) && !memcmp(mid, got, 16));
        assert(!falafel_async_encrypt(&crypto, &a, mid, plain, len, again, sizeof(again), &k));
        assert(k == n && memcmp(wire, again, n));
    }
    /* Replay acceptance is explicit: persistent deduplication is a later layer. */
    assert(!falafel_async_decrypt(&crypto, &b, wire, n, out, sizeof(out), &m, got));
    assert(!falafel_async_decrypt(&crypto, &b, wire, n, out, sizeof(out), &m, got));
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, SIZE_MAX, again, 256, &m));
    assert(falafel_async_decrypt(&crypto, &b, wire, SIZE_MAX, out, 123, &m, got));
    assert(falafel_async_encrypt(&crypto, NULL, mid, plain, 1, again, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, NULL, plain, 1, again, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, 1, NULL, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, 1, again, 256, NULL));
    assert(falafel_async_decrypt(&crypto, NULL, wire, n, out, 123, &m, got));
    assert(falafel_async_decrypt(&crypto, &b, NULL, n, out, 123, &m, got));
    assert(falafel_async_decrypt(&crypto, &b, wire, n, NULL, 123, &m, got));
    assert(falafel_async_decrypt(&crypto, &b, wire, n, out, 123, NULL, got));
    assert(falafel_async_decrypt(&crypto, &b, wire, n, out, 123, &m, NULL));
    /* Every byte is bound, including all header fields, ephemeral key and tag. */
    for (size_t i = 0; i < n; ++i) {
        memcpy(altered, wire, n); altered[i] ^= 1; memset(out, 0xaa, sizeof(out)); m = 99;
        assert(falafel_async_decrypt(&crypto, &b, altered, n, out, sizeof(out), &m, got));
        assert(m == 0); for (size_t j = 0; j < 122; ++j) assert(out[j] == 0);
        for (size_t j = 0; j < 16; ++j) assert(got[j] == 0);
    }
    for (size_t i = 0; i < n; ++i)
        assert(falafel_async_decrypt(&crypto, &b, wire, i, out, sizeof(out), &m, got));
    assert(falafel_async_decrypt(&crypto, &b, wire, 257, out, sizeof(out), &m, got));
    assert(falafel_async_decrypt(&crypto, &stranger, wire, n, out, sizeof(out), &m, got));
    bad = b; memcpy(bad.peer_public, stranger.local_public, 32);
    assert(falafel_async_decrypt(&crypto, &bad, wire, n, out, sizeof(out), &m, got));
    bad = a; bad.has_pairing = 0;
    assert(falafel_async_encrypt(&crypto, &bad, mid, plain, 1, again, 256, &m));
    bad = a; bad.local_public[0] ^= 1;
    assert(falafel_async_encrypt(&crypto, &bad, mid, plain, 1, again, 256, &m));
    bad = a; memset(bad.peer_public, 0, 32); bad.peer_public[0] = 1;
    assert(falafel_async_encrypt(&crypto, &bad, mid, plain, 1, again, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, 123, again, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, mid, NULL, 1, again, 256, &m));
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, 122, again, 255, &m));
    assert(falafel_async_decrypt(&crypto, &b, wire, n, out, 121, &m, got));
    assert(falafel_async_encrypt(NULL, &a, mid, plain, 1, again, 256, &m));
    assert(falafel_async_decrypt(NULL, &b, wire, n, out, 123, &m, got));
    falafel_crypto_free(&crypto);
    assert(falafel_async_encrypt(&crypto, &a, mid, plain, 1, again, 256, &m));
    assert(falafel_async_decrypt(&crypto, &b, wire, n, out, 123, &m, got));
    falafel_crypto_secure_zero(&a, sizeof(a)); falafel_crypto_secure_zero(&b, sizeof(b));
    falafel_crypto_secure_zero(&stranger, sizeof(stranger)); falafel_crypto_secure_zero(&bad, sizeof(bad));
    puts("async E2EE: PASS"); return 0;
}
