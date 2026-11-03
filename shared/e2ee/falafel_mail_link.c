#include "falafel_mail_link.h"
#include <string.h>
#include <stdbool.h>
static void put32(uint8_t *p, uint32_t n)
{ for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(24-8*i)); }
static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
int falafel_mail_frame_decode(const uint8_t *p, size_t n, falafel_mail_frame *f)
{
    if(!p || !f || n<30 || n>286 || memcmp(p,"FL\2",3) || (p[3]!=4 && p[3]!=5)) return -1;
    size_t len=((size_t)p[28]<<8)|p[29];
    if(len<134 || len>256 || n!=30+len || memcmp(p+30,"FAE\1\1",5) ||
       p[35]>122 || len!=134u+p[35] || memcmp(p+12,p+100,16) ||
       (p[3]==4 && len<139) || (p[3]==5 && len!=172)) return -1;
    f->source=get32(p+4); f->destination=get32(p+8);
    if(!f->source || !f->destination || f->source==f->destination ||
       f->source==UINT32_MAX || f->destination==UINT32_MAX) return -1;
    f->kind=p[3]; memcpy(f->id,p+12,16); f->envelope=p+30; f->size=len; return 0;
}
int falafel_mail_frame_encode(const falafel_mail_frame *f, uint8_t out[286], size_t *n)
{
    falafel_mail_frame check;
    if(n) *n=0;
    if(!f || !out || !n || !f->envelope || f->size>256) return -1;
    memcpy(out,"FL\2",3); out[3]=f->kind; put32(out+4,f->source); put32(out+8,f->destination);
    memcpy(out+12,f->id,16); out[28]=(uint8_t)(f->size>>8); out[29]=(uint8_t)f->size;
    memcpy(out+30,f->envelope,f->size);
    if(falafel_mail_frame_decode(out,30+f->size,&check)) return -1;
    *n=30+f->size; return 0;
}
void falafel_mail_link_init(falafel_mail_link *l, falafel_mailbox *m, uint32_t own,
                           uint32_t peer, falafel_mail_send_fn send, void *u)
{
    memset(l,0,sizeof(*l)); l->mailbox=m; l->own=own; l->peer=peer; l->send=send; l->user=u;
}
void falafel_mail_link_connected(falafel_mail_link *l)
{ l->ack_mask=255; memset(l->due,0,sizeof(l->due)); }
falafel_mail_result falafel_mail_link_receive(falafel_mail_link *l, const uint8_t *p, size_t n)
{
    falafel_mail_frame f; uint8_t ack[256]; size_t an;
    if(!l || !l->mailbox->ready || falafel_mail_frame_decode(p,n,&f) ||
       f.source!=l->peer || f.destination!=l->own) return FALAFEL_MAIL_INVALID;
    falafel_mail_result r;
    if(f.kind==5) r=falafel_mail_ack(l->mailbox,f.envelope,f.size);
    else {
        r=falafel_mail_receive(l->mailbox,f.envelope,f.size,ack,&an);
        bool active=false;
        if(r==FALAFEL_MAIL_OK || r==FALAFEL_MAIL_DUPLICATE) {
            for(unsigned i=0;i<8;++i) if(l->mailbox->inbox[i].state &&
                !memcmp(l->mailbox->inbox[i].envelope+70,f.id,16)) { l->ack_mask|=1u<<i; active=true; }
            if(!active && an==172) {
                bool queued=false;
                for(unsigned i=0;i<l->retired_ack_count;++i)
                    if(!memcmp(l->retired_ack[i]+70,f.id,16)) queued=true;
                if(!queued && l->retired_ack_count<8)
                    memcpy(l->retired_ack[l->retired_ack_count++],ack,172);
                /* A full volatile ACK queue is safe: the sender retries and
                 * the exact authenticated receipt remains in durable archive. */
            }
        }
    }
    l->last_receive=r; return r;
}
void falafel_mail_link_tick(falafel_mail_link *l, uint64_t now)
{
    if(!l || !l->mailbox->ready || !l->send) return;
    for(unsigned step=0;step<17;++step) {
        unsigned index=(l->cursor+step)%17, slot=index%8; uint8_t env[256], wire[286]; size_t n,wn;
        falafel_mail_frame f={0};
        if(index<8) {
            if(now<l->due[slot] || falafel_mail_retry(l->mailbox,slot,env,&n)!=FALAFEL_MAIL_OK) continue;
            f.kind=4;
        } else if(index<16) {
            falafel_mail_record *r=&l->mailbox->inbox[slot];
            if(!(l->ack_mask&(1u<<slot)) || !r->state) continue;
            n=r->ack_size; memcpy(env,r->ack,n); f.kind=5;
        } else {
            if(!l->retired_ack_count) continue;
            n=172; memcpy(env,l->retired_ack[0],n); f.kind=5;
        }
        f.source=l->own; f.destination=l->peer; memcpy(f.id,env+70,16); f.envelope=env; f.size=n;
        if(falafel_mail_frame_encode(&f,wire,&wn) || !l->send(l->user,wire,wn)) return;
        if(index<8) l->due[slot]=now+1000;
        else if(index<16) l->ack_mask&=~(1u<<slot);
        else {
            --l->retired_ack_count;
            memmove(l->retired_ack[0],l->retired_ack[1],l->retired_ack_count*172);
        }
        l->cursor=(index+1)%17; return;
    }
}
falafel_mail_result falafel_mail_link_consume(falafel_mail_link *l, falafel_mail_effect_fn effect, void *u)
{
    uint8_t id[16], plain[117]; size_t n;
    if(!l || !effect) return FALAFEL_MAIL_INVALID;
    falafel_mail_result r=falafel_mail_peek(l->mailbox,id,plain,&n);
    if(r==FALAFEL_MAIL_OK) {
        if(effect(u,l->mailbox->identity->peer_public,id,plain,n)) r=FALAFEL_MAIL_IO;
        else r=falafel_mail_consume(l->mailbox,id);
    }
    falafel_crypto_secure_zero(plain,sizeof(plain)); return r;
}
falafel_mail_result falafel_mail_link_reclaim(falafel_mail_link *l)
{
    if(!l || !l->mailbox) return FALAFEL_MAIL_INVALID;
    for(unsigned i=0;i<8;++i) if(l->mailbox->inbox[i].state==2) {
        const uint8_t *ack=l->mailbox->inbox[i].ack; bool queued=false;
        for(unsigned j=0;j<l->retired_ack_count;++j)
            if(!memcmp(l->retired_ack[j]+70,ack+70,16)) queued=true;
        if(!queued && l->retired_ack_count<8) memcpy(l->retired_ack[l->retired_ack_count++],ack,172);
    }
    falafel_mail_result result=falafel_mail_reclaim(l->mailbox);
    for(unsigned i=0;i<8;++i) {
        if(!l->mailbox->outbox[i].state) l->due[i]=0;
        if(!l->mailbox->inbox[i].state) l->ack_mask&=~(1u<<i);
    }
    return result;
}
