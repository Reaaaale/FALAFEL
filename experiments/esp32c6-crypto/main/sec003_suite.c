#include "sec003_suite.h"
#include "sec003_probe.h"
#include "mbedtls/ctr_drbg.h"
#include <string.h>

_Static_assert(MBEDTLS_CTR_DRBG_RESEED_INTERVAL > 0 &&
               MBEDTLS_CTR_DRBG_RESEED_INTERVAL <= 100000, "Unreviewed reseed test budget");
_Static_assert(MBEDTLS_CTR_DRBG_MAX_REQUEST <= 4096, "Unreviewed random buffer size");
/* Static scratch avoids the original C6 task-stack problem. All exits wipe it.
 * There is exactly one owner; these are disposable keys, never NVS/RTC data. */
static struct {
    uint8_t private_key[2][32], public_key[2][32], shared[2][32];
    uint8_t key[32], nonce[12], aad[16], tag[16];
    uint8_t plain[117], cipher[117], recovered[117], buf[2][64], guard[34];
    uint8_t oversized[MBEDTLS_CTR_DRBG_MAX_REQUEST + 1];
} w;
static falafel_crypto_context_t *crypto;
static const sec003_hooks *hooks;
static sec003_result *result;
static bool live;

static bool zero(const void *data, size_t n)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}
static bool hex(const char *text, uint8_t *out, size_t n)
{
    if (strlen(text) != n * 2) return false;
    for (size_t i = 0; i < n * 2; ++i) {
        char c = text[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        if (!(i & 1)) out[i / 2] = (uint8_t)(v << 4); else out[i / 2] |= (uint8_t)v;
    }
    return true;
}
static void pause_test(unsigned n)
{ if (!(n % 64) && hooks && hooks->yield) hooks->yield(hooks->user); }
static bool observed_hardware_since(sec003_probe_stats before)
{
#ifdef ESP_PLATFORM
    sec003_probe_stats now = sec003_probe_get();
    return now.hardware_calls > before.hardware_calls && now.hardware_bytes > before.hardware_bytes;
#else
    (void)before;
    return true; /* Native OS entropy: no ESP hardware claim. */
#endif
}

static bool init_failure(void)
{
    sec003_probe_fail_next();
    falafel_crypto_result_t r = falafel_crypto_init(crypto);
    live = r == FALAFEL_CRYPTO_OK;
    sec003_probe_cancel_failure();
    return r == FALAFEL_CRYPTO_RANDOM_FAILED && zero(crypto, sizeof(*crypto)) &&
           sec003_probe_get().injected_failures == 1;
}
static bool init(void)
{
    sec003_probe_stats before = sec003_probe_get();
    live = falafel_crypto_init(crypto) == FALAFEL_CRYPTO_OK;
    return live && sec003_probe_get().entropy_calls > before.entropy_calls && observed_hardware_since(before);
}
static bool rng_sanity(void)
{
    /* Gross repeat/zero checks only. These do NOT estimate entropy. */
    for (unsigned i = 0; i < 32; ++i) {
        memcpy(w.buf[1], w.buf[0], 32);
        if (falafel_crypto_random(crypto, w.buf[0], 32) || zero(w.buf[0], 32) ||
            !memcmp(w.buf[0], w.buf[1], 32)) return false;
    }
    return true;
}
static bool automatic_reseed(void)
{
    sec003_probe_stats before = sec003_probe_get();
    for (unsigned i = 0; i < MBEDTLS_CTR_DRBG_RESEED_INTERVAL + 2u; ++i) {
        if (falafel_crypto_random(crypto, w.buf[0], 16)) return false;
        pause_test(i);
        if (sec003_probe_get().entropy_calls > before.entropy_calls)
            return observed_hardware_since(before);
    }
    return false;
}
static bool reseed_failure_recovery(void)
{
    sec003_probe_stats before = sec003_probe_get();
    sec003_probe_fail_next();
    bool failed = false;
    for (unsigned i = 0; i < MBEDTLS_CTR_DRBG_RESEED_INTERVAL + 2u; ++i) {
        falafel_crypto_result_t r = falafel_crypto_random(crypto, w.buf[0], 16);
        pause_test(i);
        if (r != FALAFEL_CRYPTO_OK) { failed = r == FALAFEL_CRYPTO_RANDOM_FAILED; break; }
    }
    sec003_probe_cancel_failure();
    falafel_crypto_secure_zero(w.buf, sizeof(w.buf));
    if (!failed || sec003_probe_get().injected_failures != before.injected_failures + 1) return false;
    before = sec003_probe_get();
    return !falafel_crypto_random(crypto, w.buf[0], 16) &&
           sec003_probe_get().entropy_calls > before.entropy_calls && observed_hardware_since(before);
}
static bool hkdf_kat(void)
{
    uint8_t ikm[22], salt[13], info[10]; memset(ikm, 0x0b, sizeof(ikm));
    for (unsigned i = 0; i < sizeof(salt); ++i) salt[i] = (uint8_t)i;
    for (unsigned i = 0; i < sizeof(info); ++i) info[i] = (uint8_t)(0xf0 + i);
    return hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", w.buf[1], 42) &&
           !falafel_crypto_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), w.buf[0], 42) &&
           !memcmp(w.buf[0], w.buf[1], 42);
}
static bool gcm_kat(void)
{
    memset(w.key, 0, sizeof(w.key)); memset(w.nonce, 0, sizeof(w.nonce)); memset(w.plain, 0, 16);
    if (!hex("cea7403d4d606b6e074ec5d3baf39d18", w.buf[1], 16) ||
        falafel_crypto_aead_encrypt(w.key, 32, w.nonce, 12, NULL, 0, w.plain, 16, w.buf[0], 16, w.tag, 16) ||
        memcmp(w.buf[0], w.buf[1], 16) ||
        !hex("d0d1c8a799996bf0265b98b5d48ab919", w.buf[1], 16) || memcmp(w.tag, w.buf[1], 16)) return false;
    memset(w.recovered, 0xa5, sizeof(w.recovered));
    return !falafel_crypto_aead_decrypt(w.key, 32, w.nonce, 12, NULL, 0, w.buf[0], 16,
                                      w.tag, 16, w.recovered, sizeof(w.recovered)) && zero(w.recovered, 16);
}
static bool x25519_rfc(void)
{
    if (!hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", w.private_key[0], 32) ||
        !hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", w.public_key[1], 32) ||
        !hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", w.buf[0], 32) ||
        falafel_crypto_x25519_shared(crypto, w.private_key[0], 32, w.public_key[1], 32, w.shared[0], 32) ||
        memcmp(w.shared[0], w.buf[0], 32) ||
        !hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", w.buf[0], 32) ||
        falafel_crypto_x25519_public_from_private(crypto, w.private_key[0], 32, w.public_key[0], 32) ||
        memcmp(w.public_key[0], w.buf[0], 32)) return false;
    static const char *low[] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0100000000000000000000000000000000000000000000000000000000000000",
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"
    };
    for (unsigned i = 0; i < sizeof(low) / sizeof(low[0]); ++i) {
        if (!hex(low[i], w.public_key[1], 32)) return false;
        memset(w.shared[0], 0xa5, 32);
        if (falafel_crypto_x25519_shared(crypto, w.private_key[0], 32, w.public_key[1], 32, w.shared[0], 32) == FALAFEL_CRYPTO_OK ||
            !zero(w.shared[0], 32)) return false;
    }
    return true;
}
static bool x25519_ephemeral(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (falafel_crypto_x25519_generate(crypto, w.private_key[i], 32, w.public_key[i], 32) ||
            falafel_crypto_x25519_public_from_private(crypto, w.private_key[i], 32, w.buf[0], 32) ||
            memcmp(w.public_key[i], w.buf[0], 32)) return false;
    }
    if (!memcmp(w.public_key[0], w.public_key[1], 32)) return false;
    for (unsigned i = 0; i < 2; ++i)
        if (falafel_crypto_x25519_shared(crypto, w.private_key[i], 32, w.public_key[1 - i], 32, w.shared[i], 32)) return false;
    if (zero(w.shared[0], 32) || !falafel_crypto_constant_time_equal(w.shared[0], w.shared[1], 32)) return false;
    static const uint8_t info[] = "SEC003 disposable AES-GCM test only";
    if (falafel_crypto_hkdf_sha256(NULL, 0, w.shared[0], 32, info, sizeof(info) - 1, w.key, 32)) return false;
    memcpy(w.buf[0], w.public_key[0], 32); memcpy(w.buf[0] + 32, w.public_key[1], 32);
    if (falafel_crypto_sha256(w.buf[0], 64, result->public_boot_fingerprint)) return false;
    falafel_crypto_secure_zero(w.private_key, sizeof(w.private_key));
    falafel_crypto_secure_zero(w.shared, sizeof(w.shared));
    return true;
}
static bool gcm_roundtrip(void)
{
    if (falafel_crypto_random(crypto, w.nonce, sizeof(w.nonce))) return false;
    for (unsigned i = 0; i < sizeof(w.plain); ++i) w.plain[i] = (uint8_t)(i ^ 0x5a);
    for (unsigned i = 0; i < sizeof(w.aad); ++i) w.aad[i] = (uint8_t)i;
    return !falafel_crypto_aead_encrypt(w.key, 32, w.nonce, 12, w.aad, sizeof(w.aad),
                                       w.plain, sizeof(w.plain), w.cipher, sizeof(w.cipher), w.tag, 16) &&
           !falafel_crypto_aead_decrypt(w.key, 32, w.nonce, 12, w.aad, sizeof(w.aad),
                                       w.cipher, sizeof(w.cipher), w.tag, 16, w.recovered, sizeof(w.recovered)) &&
           !memcmp(w.plain, w.recovered, sizeof(w.plain));
}
static bool rejected_gcm(void)
{
    memset(w.recovered, 0xa5, sizeof(w.recovered));
    return falafel_crypto_aead_decrypt(w.key, 32, w.nonce, 12, w.aad, sizeof(w.aad),
                                      w.cipher, sizeof(w.cipher), w.tag, 16, w.recovered, sizeof(w.recovered)) == FALAFEL_CRYPTO_AUTH_FAILED &&
           zero(w.recovered, sizeof(w.recovered));
}
static bool gcm_tamper(void)
{
    w.tag[0] ^= 1; if (!rejected_gcm()) return false; w.tag[0] ^= 1;
    w.cipher[0] ^= 1; if (!rejected_gcm()) return false; w.cipher[0] ^= 1;
    w.aad[0] ^= 1; if (!rejected_gcm()) return false; w.aad[0] ^= 1;
    return true;
}
static bool invalid_arguments(void)
{
    memset(w.guard, 0xa5, sizeof(w.guard));
    if (falafel_crypto_random(NULL, w.buf[0], 16) != FALAFEL_CRYPTO_INVALID_ARGUMENT ||
        falafel_crypto_x25519_shared(crypto, w.private_key[0], 32, w.public_key[0], 32, w.guard + 1, 31) != FALAFEL_CRYPTO_INVALID_ARGUMENT ||
        falafel_crypto_aead_encrypt(w.key, 32, w.nonce, 11, NULL, 0, w.plain, 16, w.guard, 16, w.tag, 16) != FALAFEL_CRYPTO_INVALID_ARGUMENT ||
        falafel_crypto_hkdf_sha256(NULL, 0, w.plain, 0, NULL, 0, w.guard, 32) != FALAFEL_CRYPTO_INVALID_ARGUMENT) return false;
    for (unsigned i = 0; i < sizeof(w.guard); ++i) if (w.guard[i] != 0xa5) return false;
    return falafel_crypto_random(crypto, w.oversized, sizeof(w.oversized)) == FALAFEL_CRYPTO_RANDOM_FAILED;
}
static bool cleanup_reinit(void)
{
    falafel_crypto_free(crypto); live = false;
    if (!zero(crypto, sizeof(*crypto)) ||
        falafel_crypto_random(crypto, w.buf[0], 16) != FALAFEL_CRYPTO_INVALID_ARGUMENT) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!init()) return false;
        if (falafel_crypto_random(crypto, w.buf[0], 32)) return false;
        falafel_crypto_free(crypto); live = false;
        if (!zero(crypto, sizeof(*crypto))) return false;
    }
    return true;
}

bool sec003_suite_run(falafel_crypto_context_t *context, const sec003_hooks *callbacks, sec003_result *out)
{
    if (!context || !out) return false;
    crypto = context; hooks = callbacks; result = out; live = false;
    memset(out, 0, sizeof(*out)); memset(&w, 0, sizeof(w)); sec003_probe_reset();
#define RUN(name) do { \
    bool ok = name(); \
    if (ok) ++out->passed; else ++out->failed; \
    if (hooks && hooks->case_result) hooks->case_result(hooks->user, #name, ok); \
    if (!ok) goto finished; \
} while (0)
    RUN(init_failure); RUN(init); RUN(rng_sanity); RUN(automatic_reseed);
    RUN(reseed_failure_recovery); RUN(hkdf_kat); RUN(gcm_kat); RUN(x25519_rfc);
    RUN(x25519_ephemeral); RUN(gcm_roundtrip); RUN(gcm_tamper);
    RUN(invalid_arguments); RUN(cleanup_reinit);
#undef RUN
finished:
    sec003_probe_cancel_failure();
    if (live) falafel_crypto_free(crypto);
    live = false;
    falafel_crypto_secure_zero(&w, sizeof(w));
    out->cleaned = zero(crypto, sizeof(*crypto)) && zero(&w, sizeof(w));
    return !out->failed && out->passed == SEC003_CORE_CASES && out->cleaned;
}
