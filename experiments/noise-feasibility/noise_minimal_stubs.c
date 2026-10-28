/* Feasibility-only link stubs: exclude algorithms outside the selected suite. */
#include <stddef.h>

typedef struct NoiseCipherState_s NoiseCipherState;
typedef struct NoiseHashState_s NoiseHashState;
typedef struct NoiseDHState_s NoiseDHState;
typedef struct NoiseSignState_s NoiseSignState;

NoiseCipherState *noise_chachapoly_new(void) { return NULL; }
NoiseHashState *noise_blake2s_new(void) { return NULL; }
NoiseHashState *noise_blake2b_new(void) { return NULL; }
NoiseHashState *noise_sha512_new(void) { return NULL; }
NoiseDHState *noise_curve448_new(void) { return NULL; }
NoiseDHState *noise_newhope_new(void) { return NULL; }
NoiseSignState *noise_ed25519_new(void) { return NULL; }

extern int curve25519_donna(unsigned char *result,
                           const unsigned char *secret,
                           const unsigned char *basepoint);
void curved25519_scalarmult_basepoint(unsigned char *result,
                                     const unsigned char *secret)
{
    static const unsigned char basepoint[32] = {9};
    (void)curve25519_donna(result, secret, basepoint);
}
