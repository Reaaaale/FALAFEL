#include "falafel_async_e2ee.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned calls, fail_at;
static falafel_crypto_result_t fault_x25519_public_from_private(falafel_crypto_context_t *c, const uint8_t *p, size_t pn, uint8_t *o, size_t on) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_x25519_public_from_private(c,p,pn,o,on); }
static falafel_crypto_result_t fault_x25519_generate(falafel_crypto_context_t *c, uint8_t *p, size_t pn, uint8_t *o, size_t on) { if (++calls == fail_at) return FALAFEL_CRYPTO_RANDOM_FAILED; return falafel_crypto_x25519_generate(c,p,pn,o,on); }
static falafel_crypto_result_t fault_x25519_shared(falafel_crypto_context_t *c, const uint8_t *p, size_t pn, const uint8_t *q, size_t qn, uint8_t *o, size_t on) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_x25519_shared(c,p,pn,q,qn,o,on); }
static falafel_crypto_result_t fault_sha256(const uint8_t *p, size_t n, uint8_t o[32]) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_sha256(p,n,o); }
static falafel_crypto_result_t fault_hkdf_sha256(const uint8_t *s, size_t sn, const uint8_t *p, size_t pn, const uint8_t *i, size_t in, uint8_t *o, size_t on) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_hkdf_sha256(s,sn,p,pn,i,in,o,on); }
static falafel_crypto_result_t fault_aead_encrypt(const uint8_t *k, size_t kn, const uint8_t *v, size_t vn, const uint8_t *a, size_t an, const uint8_t *p, size_t pn, uint8_t *o, size_t on, uint8_t *t, size_t tn) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_aead_encrypt(k,kn,v,vn,a,an,p,pn,o,on,t,tn); }
static falafel_crypto_result_t fault_aead_decrypt(const uint8_t *k, size_t kn, const uint8_t *v, size_t vn, const uint8_t *a, size_t an, const uint8_t *p, size_t pn, const uint8_t *t, size_t tn, uint8_t *o, size_t on) { if (++calls == fail_at) return FALAFEL_CRYPTO_BACKEND_ERROR; return falafel_crypto_aead_decrypt(k,kn,v,vn,a,an,p,pn,t,tn,o,on); }
#define falafel_crypto_x25519_public_from_private fault_x25519_public_from_private
#define falafel_crypto_x25519_generate fault_x25519_generate
#define falafel_crypto_x25519_shared fault_x25519_shared
#define falafel_crypto_sha256 fault_sha256
#define falafel_crypto_hkdf_sha256 fault_hkdf_sha256
#define falafel_crypto_aead_encrypt fault_aead_encrypt
#define falafel_crypto_aead_decrypt fault_aead_decrypt
#include "../shared/e2ee/falafel_async_e2ee.c"
#undef falafel_crypto_x25519_public_from_private
#undef falafel_crypto_x25519_generate
#undef falafel_crypto_x25519_shared
#undef falafel_crypto_sha256
#undef falafel_crypto_hkdf_sha256
#undef falafel_crypto_aead_encrypt
#undef falafel_crypto_aead_decrypt
int main(void) {
 falafel_crypto_context_t c; falafel_pairing_persistent_state a = {0}, b = {0};
 uint8_t wire[256], out[256], mid[16]={1}, got[16], plain[1]={42}; size_t n,m;
 assert(!falafel_crypto_init(&c));
 assert(!falafel_crypto_x25519_generate(&c,a.local_private,32,a.local_public,32));
 assert(!falafel_crypto_x25519_generate(&c,b.local_private,32,b.local_public,32));
 memcpy(a.peer_public,b.local_public,32); memcpy(b.peer_public,a.local_public,32);
 a.has_pairing=b.has_pairing=1; a.generation=b.generation=1;
 assert(!falafel_async_encrypt(&c,&a,mid,plain,1,wire,256,&n));
 unsigned enc_calls=calls; calls=0;
 assert(!falafel_async_decrypt(&c,&b,wire,n,out,256,&m,got));
 unsigned dec_calls=calls;
 for(unsigned op=0;op<2;++op) for(unsigned i=1;i<=(op?dec_calls:enc_calls);++i) {
   calls=0; fail_at=i; memset(out,0xaa,256); m=99;
   assert(op ? falafel_async_decrypt(&c,&b,wire,n,out,256,&m,got) :
               falafel_async_encrypt(&c,&a,mid,plain,1,out,256,&m));
   assert(m==0); for(unsigned j=0;j<(op?122:256);++j) assert(out[j]==0);
 }
 falafel_crypto_secure_zero(&a,sizeof(a)); falafel_crypto_secure_zero(&b,sizeof(b));
 falafel_crypto_free(&c); puts("provider fault injection: PASS"); return 0;
}
