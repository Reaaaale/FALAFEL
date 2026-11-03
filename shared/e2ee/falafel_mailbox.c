#include "falafel_mailbox.h"
#include <string.h>
#define RECORDS 16u
static const uint8_t data_label[5] = {'F','M','S','G',1};
static const uint8_t ack_label[6] = {'F','A','C','K',1,1}; /* status: durable acceptance */
static int usable(const falafel_mailbox *m)
{ return m && m->ready && (m->format_version==1 || (m->retention.lookup && m->retention.put)); }
static void put16(uint8_t *p, unsigned n) { p[0]=(uint8_t)(n>>8); p[1]=(uint8_t)n; }
static unsigned get16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }
static int envelope(const uint8_t *p, size_t n, const uint8_t *sender, const uint8_t *recipient)
{
    return n>=134 && n<=256 && !memcmp(p,"FAE\1\1",5) && p[5]<=122 && n==134u+p[5] &&
        !memcmp(p+6,sender,32) && !memcmp(p+38,recipient,32);
}
static int zero(const uint8_t *p, size_t n)
{ uint8_t v=0; while(n--) v|=*p++; return v==0; }
static falafel_mail_record *record(falafel_mailbox *m, unsigned i)
{ return i<8 ? &m->outbox[i] : &m->inbox[i-8]; }
static int find(falafel_mail_record *r, const uint8_t id[16])
{ for(int i=0;i<8;++i) if(r[i].state && !memcmp(r[i].envelope+70,id,16)) return i; return -1; }
static int vacant(falafel_mail_record *r)
{ for(int i=0;i<8;++i) if(!r[i].state) return i; return -1; }
static int retired(falafel_mailbox *m, int outgoing, const uint8_t id[16], falafel_mail_record *r)
{
    if (!m->retention.lookup) return 1;
    int result = m->retention.lookup(m->retention.user, outgoing, id, r);
    if (result < 0) m->ready = 0;
    return result;
}
/* Serialize an optional replacement without mutating live state. */
static falafel_mail_result commit(falafel_mailbox *m, int slot, const falafel_mail_record *next)
{
    unsigned header=m->format_version==2?88u:72u;
    size_t total=header+16u*FALAFEL_MAILBOX_RECORD_SIZE+32u;
    uint8_t *p=m->scratch; memset(p,0,total);
    memcpy(p,"FMBX",4); p[4]=(uint8_t)m->format_version; memcpy(p+8,m->identity->local_public,32);
    memcpy(p+40,m->identity->peer_public,32);
    if(m->format_version==2) memcpy(p+72,m->archive_id,16);
    for(unsigned i=0;i<RECORDS;++i) {
        const falafel_mail_record *r=(int)i==slot?next:record(m,i);
        uint8_t *q=p+header+i*FALAFEL_MAILBOX_RECORD_SIZE;
        q[0]=r->state; put16(q+1,r->size); put16(q+3,r->ack_size);
        memcpy(q+5,r->envelope,256); memcpy(q+261,r->ack,256);
    }
    if(falafel_crypto_sha256(p,total-32,p+total-32)) return FALAFEL_MAIL_AUTH;
    if(m->storage.replace(m->storage.user,p,total)) {
        m->ready=0; return FALAFEL_MAIL_IO;
    }
    if(slot>=0) *record(m,(unsigned)slot)=*next;
    return FALAFEL_MAIL_OK;
}
falafel_mail_result falafel_mail_open(falafel_mailbox *m, falafel_mail_storage storage,
    falafel_crypto_context_t *c, const falafel_pairing_persistent_state *id, int create)
{
    uint8_t hash[32], pub[32]; size_t n=0; int status;
    if(m) m->ready=0;
    if(!m || !c || !id || !storage.load || !storage.replace || id->has_pairing!=1 ||
       !id->generation || zero(id->local_private,32) || zero(id->peer_public,32) ||
       falafel_crypto_x25519_public_from_private(c,id->local_private,32,pub,32) ||
       memcmp(pub,id->local_public,32)) return FALAFEL_MAIL_INVALID;
    memset(m,0,sizeof(*m)); m->storage=storage; m->identity=id; m->crypto=c; m->format_version=1;
    status=storage.load(storage.user,m->scratch,sizeof(m->scratch),&n);
    if(status==1) {
        if(!create) return FALAFEL_MAIL_NOT_FOUND;
        falafel_mail_result r=commit(m,-1,NULL); m->ready=r==FALAFEL_MAIL_OK; return r;
    }
    if(status) return FALAFEL_MAIL_IO;
    if(n<8 || memcmp(m->scratch,"FMBX",4) ||
       (m->scratch[4]!=1 && m->scratch[4]!=2) || !zero(m->scratch+5,3) ||
       n!=(m->scratch[4]==2?FALAFEL_MAILBOX_MAX_SNAPSHOT_SIZE:FALAFEL_MAILBOX_SNAPSHOT_SIZE) ||
       memcmp(m->scratch+8,id->local_public,32) || memcmp(m->scratch+40,id->peer_public,32) ||
       falafel_crypto_sha256(m->scratch,n-32,hash) ||
       !falafel_crypto_constant_time_equal(hash,m->scratch+n-32,32)) return FALAFEL_MAIL_CORRUPT;
    unsigned header=m->scratch[4]==2?88u:72u;
    if(header==88) {
        if(zero(m->scratch+72,16)) return FALAFEL_MAIL_CORRUPT;
        memcpy(m->archive_id,m->scratch+72,16);
    }
    for(unsigned i=0;i<RECORDS;++i) {
        const uint8_t *q=m->scratch+header+i*517; falafel_mail_record *r=record(m,i);
        r->state=q[0]; r->size=(uint16_t)get16(q+1); r->ack_size=(uint16_t)get16(q+3);
        if(!r->state) { if(!zero(q,517)) return FALAFEL_MAIL_CORRUPT; continue; }
        if(r->state>2 || r->size>256 || r->ack_size>256) return FALAFEL_MAIL_CORRUPT;
        memcpy(r->envelope,q+5,256); memcpy(r->ack,q+261,256);
        if(!envelope(r->envelope,r->size,i<8?id->local_public:id->peer_public,
                     i<8?id->peer_public:id->local_public) ||
           !zero(r->envelope+r->size,256-r->size) || !zero(r->ack+r->ack_size,256-r->ack_size) ||
           (i<8 ? r->ack_size!=0 : (r->ack_size!=172 ||
             !envelope(r->ack,r->ack_size,id->local_public,id->peer_public) ||
             memcmp(r->ack+70,r->envelope+70,16)))) return FALAFEL_MAIL_CORRUPT;
        for(unsigned j=i<8?0:8;j<i;++j)
            if(record(m,j)->state && !memcmp(record(m,j)->envelope+70,r->envelope+70,16))
                return FALAFEL_MAIL_CORRUPT;
    }
    m->format_version=m->scratch[4]; m->ready=1; return FALAFEL_MAIL_OK;
}
falafel_mail_result falafel_mail_enqueue(falafel_mailbox *m, const uint8_t id[16],
                                         const uint8_t *text, size_t n)
{
    uint8_t plain[122]={0}; falafel_mail_record next={0}; size_t size=0;
    falafel_mail_result r=FALAFEL_MAIL_AUTH; int slot;
    if(!usable(m) || !id || (!text && n) || n>117) return FALAFEL_MAIL_INVALID;
    if(find(m->outbox,id)>=0) return FALAFEL_MAIL_CONFLICT; /* never re-encrypt an ID */
    falafel_mail_record archived;
    int found = retired(m,1,id,&archived);
    if(found<0) return FALAFEL_MAIL_IO;
    if(!found) return FALAFEL_MAIL_CONFLICT;
    slot=vacant(m->outbox); if(slot<0) return FALAFEL_MAIL_FULL;
    memcpy(plain,data_label,5); if(n) memcpy(plain+5,text,n);
    if(!falafel_async_encrypt(m->crypto,m->identity,id,plain,n+5,next.envelope,256,&size)) {
        next.state=1; next.size=(uint16_t)size; r=commit(m,slot,&next);
    }
    falafel_crypto_secure_zero(plain,sizeof(plain)); return r;
}
falafel_mail_result falafel_mail_retry(falafel_mailbox *m, unsigned slot, uint8_t out[256], size_t *n)
{
    if(n) *n=0;
    if(!usable(m) || !out || !n || slot>=8) return FALAFEL_MAIL_INVALID;
    if(m->outbox[slot].state!=1) return FALAFEL_MAIL_NOT_FOUND;
    *n=m->outbox[slot].size; memcpy(out,m->outbox[slot].envelope,*n); return FALAFEL_MAIL_OK;
}
falafel_mail_result falafel_mail_receive(falafel_mailbox *m, const uint8_t *wire, size_t n,
                                        uint8_t ack[256], size_t *ack_n)
{
    uint8_t plain[122]={0}, id[16], receipt[38]; size_t pn=0, an=0;
    falafel_mail_record next={0}; int slot; falafel_mail_result r=FALAFEL_MAIL_AUTH;
    if(ack_n) *ack_n=0;
    if(!usable(m) || !wire || !ack || !ack_n) return FALAFEL_MAIL_INVALID;
    if(falafel_async_decrypt(m->crypto,m->identity,wire,n,plain,122,&pn,id) || pn<5 ||
       memcmp(plain,data_label,5)) goto done;
    slot=find(m->inbox,id);
    if(slot>=0) {
        const falafel_mail_record *old=&m->inbox[slot];
        if(old->size!=n || memcmp(old->envelope,wire,n)) { r=FALAFEL_MAIL_CONFLICT; goto done; }
        memcpy(ack,old->ack,old->ack_size); *ack_n=old->ack_size; r=FALAFEL_MAIL_DUPLICATE; goto done;
    }
    int found = retired(m,0,id,&next);
    if(found<0) { r=FALAFEL_MAIL_IO; goto done; }
    if(!found) {
        if(next.state!=2 || next.size!=n || next.ack_size!=172 || memcmp(next.envelope,wire,n))
            { r=FALAFEL_MAIL_CONFLICT; goto done; }
        memcpy(ack,next.ack,next.ack_size); *ack_n=next.ack_size;
        r=FALAFEL_MAIL_DUPLICATE; goto done;
    }
    slot=vacant(m->inbox); if(slot<0) { r=FALAFEL_MAIL_FULL; goto done; }
    memcpy(receipt,ack_label,6);
    if(falafel_crypto_sha256(wire,n,receipt+6) ||
       falafel_async_encrypt(m->crypto,m->identity,id,receipt,38,next.ack,256,&an)) goto done;
    next.state=1; next.size=(uint16_t)n; next.ack_size=(uint16_t)an; memcpy(next.envelope,wire,n);
    r=commit(m,slot+8,&next);
    if(!r) { memcpy(ack,next.ack,an); *ack_n=an; }
done:
    falafel_crypto_secure_zero(plain,sizeof(plain)); return r;
}
falafel_mail_result falafel_mail_ack(falafel_mailbox *m, const uint8_t *ack, size_t n)
{
    uint8_t plain[122]={0}, id[16], hash[32]; size_t pn=0; int slot;
    falafel_mail_result r=FALAFEL_MAIL_AUTH; falafel_mail_record next;
    if(!usable(m) || !ack) return FALAFEL_MAIL_INVALID;
    if(falafel_async_decrypt(m->crypto,m->identity,ack,n,plain,122,&pn,id) || pn!=38 ||
       memcmp(plain,ack_label,6)) goto done;
    slot=find(m->outbox,id);
    if(slot<0) {
        int found=retired(m,1,id,&next);
        if(found) { r=found<0?FALAFEL_MAIL_IO:FALAFEL_MAIL_NOT_FOUND; goto done; }
    } else next=m->outbox[slot];
    if(falafel_crypto_sha256(next.envelope,next.size,hash) ||
       !falafel_crypto_constant_time_equal(hash,plain+6,32)) goto done;
    if(next.state==2) { r=FALAFEL_MAIL_DUPLICATE; goto done; }
    if(slot<0) { r=FALAFEL_MAIL_CORRUPT; goto done; }
    next.state=2; r=commit(m,slot,&next);
done:
    falafel_crypto_secure_zero(plain,sizeof(plain)); return r;
}
falafel_mail_result falafel_mail_peek(falafel_mailbox *m, uint8_t id[16], uint8_t text[117], size_t *n)
{
    uint8_t plain[122]={0}, mid[16]; size_t pn=0; falafel_mail_result r=FALAFEL_MAIL_NOT_FOUND;
    if(n) *n=0;
    if(!usable(m) || !id || !text || !n) return FALAFEL_MAIL_INVALID;
    for(unsigned i=0;i<8;++i) if(m->inbox[i].state==1) {
        if(falafel_async_decrypt(m->crypto,m->identity,m->inbox[i].envelope,m->inbox[i].size,
                                plain,122,&pn,mid) || pn<5 || memcmp(plain,data_label,5)) r=FALAFEL_MAIL_AUTH;
        else { memcpy(id,mid,16); *n=pn-5; memcpy(text,plain+5,*n); r=FALAFEL_MAIL_OK; }
        break;
    }
    falafel_crypto_secure_zero(plain,sizeof(plain)); return r;
}
falafel_mail_result falafel_mail_consume(falafel_mailbox *m, const uint8_t id[16])
{
    int slot; falafel_mail_record next;
    if(!usable(m) || !id) return FALAFEL_MAIL_INVALID;
    slot=find(m->inbox,id);
    if(slot<0) {
        falafel_mail_record archived;
        int found=retired(m,0,id,&archived);
        if(found<0) return FALAFEL_MAIL_IO;
        if(found) return FALAFEL_MAIL_NOT_FOUND;
        if(archived.state!=2) { m->ready=0; return FALAFEL_MAIL_CORRUPT; }
        return FALAFEL_MAIL_DUPLICATE;
    }
    next=m->inbox[slot]; if(next.state==2) return FALAFEL_MAIL_DUPLICATE;
    next.state=2; return commit(m,slot+8,&next);
}

falafel_mail_result falafel_mail_set_retention(falafel_mailbox *m, falafel_mail_retention retention)
{
    if(!m || !m->ready || !retention.lookup || !retention.put) return FALAFEL_MAIL_INVALID;
    if(zero(retention.archive_id,16) || (m->format_version==2 && memcmp(m->archive_id,retention.archive_id,16)))
        return FALAFEL_MAIL_INVALID;
    m->retention=retention; return FALAFEL_MAIL_OK;
}
falafel_mail_result falafel_mail_reclaim(falafel_mailbox *m)
{
    if(!usable(m) || !m->retention.put || m->format_version!=2) return FALAFEL_MAIL_INVALID;
    falafel_mail_record empty={0};
    for(unsigned i=0;i<16;++i) {
        falafel_mail_record *r=record(m,i);
        if(r->state!=2) continue;
        if(m->retention.put(m->retention.user,i<8,r)) { m->ready=0; return FALAFEL_MAIL_IO; }
        falafel_mail_result result=commit(m,(int)i,&empty);
        if(result!=FALAFEL_MAIL_OK) return result;
    }
    return FALAFEL_MAIL_OK;
}
falafel_mail_result falafel_mail_upgrade_retention(falafel_mailbox *m)
{
    if(!usable(m) || !m->retention.put) return FALAFEL_MAIL_INVALID;
    if(m->format_version==2) return FALAFEL_MAIL_OK;
    memcpy(m->archive_id,m->retention.archive_id,16);
    m->format_version=2;
    return commit(m,-1,NULL);
}
