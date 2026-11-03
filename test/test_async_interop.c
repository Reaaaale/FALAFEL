#include "falafel_async_e2ee.h"
#include <noise/protocol.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
 falafel_crypto_context_t c; falafel_pairing_persistent_state a={0},b={0};
 uint8_t wire[256], out[122], mid[16]={5}, got[16], payload[122]; size_t n,m;
 assert(!falafel_crypto_init(&c));
 assert(!falafel_crypto_x25519_generate(&c,a.local_private,32,a.local_public,32));
 assert(!falafel_crypto_x25519_generate(&c,b.local_private,32,b.local_public,32));
 memcpy(a.peer_public,b.local_public,32); memcpy(b.peer_public,a.local_public,32);
 a.has_pairing=b.has_pairing=1; a.generation=b.generation=1; memset(payload,0x5a,122);
 for(size_t len=0;len<=122;len+=61) for(int sending=0;sending<2;++sending) {
   NoiseHandshakeState *h=NULL; NoiseBuffer msg,p;
   assert(!falafel_async_encrypt(&c,&a,mid,payload,len,wire,256,&n));
   assert(!noise_handshakestate_new_by_name(&h,"Noise_K_25519_AESGCM_SHA256",sending?NOISE_ROLE_INITIATOR:NOISE_ROLE_RESPONDER));
   assert(!noise_dhstate_set_keypair_private(noise_handshakestate_get_local_keypair_dh(h),sending?a.local_private:b.local_private,32));
   assert(!noise_dhstate_set_public_key(noise_handshakestate_get_remote_public_key_dh(h),sending?b.local_public:a.local_public,32));
   assert(!noise_handshakestate_set_prologue(h,wire,86)); assert(!noise_handshakestate_start(h));
   if(sending) {
     noise_buffer_set_output(msg,wire+86,170); noise_buffer_set_input(p,payload,len);
     assert(!noise_handshakestate_write_message(h,&msg,&p)); assert(msg.size==len+48);
     assert(!falafel_async_decrypt(&c,&b,wire,86+msg.size,out,122,&m,got)); assert(m==len);
   } else {
     noise_buffer_set_input(msg,wire+86,n-86); noise_buffer_set_output(p,out,122);
     assert(!noise_handshakestate_read_message(h,&msg,&p)); assert(p.size==len);
   }
   assert(!memcmp(out,payload,len)); noise_handshakestate_free(h);
 }
 falafel_crypto_secure_zero(&a,sizeof(a)); falafel_crypto_secure_zero(&b,sizeof(b));
 falafel_crypto_free(&c); puts("Noise K interop: PASS"); return 0;
}
