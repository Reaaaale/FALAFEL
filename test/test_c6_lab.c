#include "lab_messaging.h"
#include "falafel_storage_espidf.h"
#include "nvs_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static falafel_lab lab;
static falafel_storage_espidf store;
static falafel_crypto_context_t crypto;
static falafel_pairing_persistent_state peer;
static falafel_mailbox partner;
static uint8_t snapshot[8392], packet[286];
static size_t snapshot_size, packet_size;
static unsigned events[5];
static int load(void *u, uint8_t *p, size_t cap, size_t *n)
{ (void)u; if (!snapshot_size) return 1; assert(cap >= snapshot_size); memcpy(p,snapshot,snapshot_size); *n=snapshot_size; return 0; }
static int replace(void *u, const uint8_t *p, size_t n)
{ (void)u; assert(n<=sizeof(snapshot)); memcpy(snapshot,p,n); snapshot_size=n; return 0; }
static int send_frame(void *u, const uint8_t *p, size_t n)
{ (void)u; if (packet_size) return 0; assert(n<=sizeof(packet)); memcpy(packet,p,n); packet_size=n; return 1; }
static void event(void *u, falafel_lab_event e, const uint8_t id[16])
{ (void)u; (void)id; ++events[e]; }
static void reverse_frame(uint8_t kind, const uint8_t *env, size_t size)
{
    falafel_mail_frame f={.kind=kind,.source=2,.destination=1,.envelope=env,.size=size};
    memcpy(f.id,env+70,16); assert(!falafel_mail_frame_encode(&f,packet,&packet_size));
}

int main(void)
{
    fake_nvs_reset(); assert(!falafel_crypto_init(&crypto));
    uint8_t credential[64], local_pub[32];
    assert(!falafel_crypto_x25519_generate(&crypto,credential,32,local_pub,32));
    assert(!falafel_crypto_x25519_generate(&crypto,peer.local_private,32,peer.local_public,32));
    memcpy(credential+32,peer.local_public,32); memcpy(peer.peer_public,local_pub,32);
    peer.has_pairing=1;peer.generation=1;
    assert(!falafel_mail_open(&partner,(falafel_mail_storage){NULL,load,replace},&crypto,&peer,1));
    assert(!falafel_storage_espidf_open(&store,"falafel_data",true));
    assert(!falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),credential,1,2,true,
                            "C6 real preset",send_frame,NULL,event,NULL));
    assert(events[LAB_STORED]==1 && lab.mailbox.outbox[0].state==1);
    uint8_t original[256];size_t original_size=lab.mailbox.outbox[0].size;
    memcpy(original,lab.mailbox.outbox[0].envelope,original_size);
    falafel_mail_link_connected(&lab.link); falafel_mail_link_tick(&lab.link,0);
    assert(packet_size && lab.mailbox.outbox[0].state==1); /* socket queue never completes */
    falafel_mail_frame data;assert(!falafel_mail_frame_decode(packet,packet_size,&data));
    assert(!memcmp(data.envelope,original,original_size));packet_size=0;
    /* Poisoned receipt shape accepted by relay, rejected cryptographically. */
    uint8_t fake[172]={0};memcpy(fake,"FAE\1\1\46",6);
    memcpy(fake+6,peer.local_public,32);memcpy(fake+38,peer.peer_public,32);memcpy(fake+70,original+70,16);
    reverse_frame(5,fake,sizeof(fake));assert(falafel_lab_receive(&lab,packet,packet_size));
    assert(events[LAB_REJECTED]==1 && events[LAB_DELIVERED]==0 && lab.mailbox.outbox[0].state==1);
    uint8_t receipt[256];size_t receipt_size;
    assert(!falafel_mail_receive(&partner,original,original_size,receipt,&receipt_size));
    /* Even a genuine receipt must NOT complete on an uncertain/failed NVS write. */
    reverse_frame(5,receipt,receipt_size);fake_nvs_fault(1,FAKE_BEFORE_SET);
    assert(!falafel_lab_receive(&lab,packet,packet_size));
    assert(events[LAB_DELIVERED]==0 && !lab.mailbox.ready);
    falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));fake_nvs_reboot();
    assert(!falafel_storage_espidf_open(&store,"falafel_data",false));
    assert(!falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),credential,1,2,false,
                            "same pending ciphertext",send_frame,NULL,event,NULL));
    assert(lab.mailbox.outbox[0].state==1 && !memcmp(lab.mailbox.outbox[0].envelope,original,original_size));
    reverse_frame(5,receipt,receipt_size);assert(falafel_lab_receive(&lab,packet,packet_size));
    assert(events[LAB_DELIVERED]==1 && lab.mailbox.outbox[0].state==2);
    assert(falafel_lab_receive(&lab,packet,packet_size));assert(events[LAB_DELIVERED]==1);
    packet_size=0;
    /* Real incoming data and exact duplicate: one persisted local consumption. */
    uint8_t mid[16];assert(!falafel_crypto_random(&crypto,mid,16));
    assert(!falafel_mail_enqueue(&partner,mid,(const uint8_t *)"Mac reverse",11));
    reverse_frame(4,partner.outbox[0].envelope,partner.outbox[0].size);
    assert(falafel_lab_receive(&lab,packet,packet_size));assert(events[LAB_RECEIVED]==1);
    assert(falafel_lab_receive(&lab,packet,packet_size));assert(events[LAB_RECEIVED]==1);
    packet_size=0;falafel_mail_link_tick(&lab.link,1000);
    assert(!falafel_mail_frame_decode(packet,packet_size,&data) && data.kind==5);
    assert(!falafel_mail_ack(&partner,data.envelope,data.size));
    /* Reboot/reopen must retain ID, ciphertext and completed delivery, not queue another preset. */
    falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));fake_nvs_reboot();
    assert(!falafel_storage_espidf_open(&store,"falafel_data",false));
    assert(!falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),credential,1,2,false,
                            "changed preset must not create another ID",send_frame,NULL,event,NULL));
    assert(events[LAB_STORED]==1 && events[LAB_RESTORED]==2);
    assert(lab.mailbox.outbox[0].state==2 && !lab.mailbox.outbox[1].state);
    assert(lab.mailbox.outbox[0].size==original_size && !memcmp(lab.mailbox.outbox[0].envelope,original,original_size));
    /* Fill remaining receive slots, then preserve FULL rather than evict IDs. */
    for (unsigned i=0;i<7;++i) {
        assert(!falafel_crypto_random(&crypto,mid,16));
        assert(!falafel_mail_enqueue(&partner,mid,(const uint8_t *)"x",1));
        reverse_frame(4,partner.outbox[i+1].envelope,partner.outbox[i+1].size);
        assert(falafel_lab_receive(&lab,packet,packet_size));
    }
    assert(events[LAB_RECEIVED]==8);
    /* New valid incoming envelope, inbox full => no consumption or ACK. */
    uint8_t plain[6]={'F','M','S','G',1,'x'},extra[256];size_t extra_size;
    assert(!falafel_crypto_random(&crypto,mid,16));
    assert(!falafel_async_encrypt(&crypto,&peer,mid,plain,6,extra,sizeof(extra),&extra_size));
    reverse_frame(4,extra,extra_size);assert(falafel_lab_receive(&lab,packet,packet_size));
    assert(events[LAB_RECEIVED]==8 && events[LAB_REJECTED]==3);
    falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));fake_nvs_reboot();
    credential[10]^=1;assert(!falafel_storage_espidf_open(&store,"falafel_data",false));
    assert(falafel_lab_open(&lab,falafel_storage_espidf_mailbox(&store),credential,1,2,false,
                          "x",send_frame,NULL,event,NULL)==FALAFEL_MAIL_CORRUPT);
    falafel_lab_close(&lab);assert(!falafel_storage_espidf_close(&store));
    falafel_crypto_free(&crypto);fake_nvs_reset();
    puts("C6 lab PASS: authenticated completion, forged/duplicate receipts, reverse DATA, NVS reopen, no new boot ID, FULL, wrong identity");
    return 0;
}
