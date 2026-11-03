#include "falafel_history.h"
#include <stdio.h>
#include <string.h>
#define BODY 744u
#define HEADER 40u
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(24-8*i)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put64(uint8_t *p,uint64_t n) { for(unsigned i=0;i<8;++i) p[i]=(uint8_t)(n>>(56-8*i)); }
static uint64_t get64(const uint8_t *p) { uint64_t n=0; for(unsigned i=0;i<8;++i) n=n<<8|p[i]; return n; }
static int find(const falafel_history *h,unsigned direction,const uint8_t id[16])
{ for(unsigned i=0;i<h->count;++i) if(h->index[i].direction==direction && !memcmp(h->index[i].id,id,16)) return (int)i; return -1; }
static void filename(char name[40],unsigned direction,const uint8_t id[16])
{
    snprintf(name,40,"%u-",direction);
    for(unsigned i=0;i<16;++i) snprintf(name+2+i*2,3,"%02x",id[i]);
    memcpy(name+34,".msg",5);
}
static falafel_history_result poison(falafel_history *h,falafel_history_result result)
{ h->ready=0; return result; }
static size_t seal(falafel_history *h,const char magic[4],unsigned direction,const uint8_t id[16],size_t n)
{
    uint8_t aad[120]; memset(h->wire,0,HEADER); memcpy(h->wire,magic,4);
    h->wire[20]=(uint8_t)direction; if(id) memcpy(h->wire+21,id,16);
    if(falafel_crypto_random(h->crypto,h->wire+8,12)) return 0;
    memcpy(aad,h->wire,HEADER); memcpy(aad+HEADER,h->identity->local_public,32);
    memcpy(aad+HEADER+32,h->identity->peer_public,32);
    memcpy(aad+104,h->archive_id,16);
    if(falafel_crypto_aead_encrypt(h->key,32,h->wire+8,12,aad,sizeof(aad),h->plain,n,
        h->wire+HEADER,n,h->wire+HEADER+n,16)) return 0;
    return HEADER+n+16;
}
static int unseal(falafel_history *h,const char magic[4],unsigned direction,const uint8_t *id,size_t n)
{
    uint8_t aad[120];
    if(n<56 || memcmp(h->wire,magic,4) || h->wire[20]!=direction ||
       (id && memcmp(h->wire+21,id,16))) return -1;
    memcpy(aad,h->wire,HEADER); memcpy(aad+HEADER,h->identity->local_public,32);
    memcpy(aad+HEADER+32,h->identity->peer_public,32);
    memcpy(aad+104,h->archive_id,16);
    return falafel_crypto_aead_decrypt(h->key,32,h->wire+8,12,aad,sizeof(aad),
        h->wire+HEADER,n-56,h->wire+n-16,16,h->plain,sizeof(h->plain)) != FALAFEL_CRYPTO_OK;
}
static void encode(falafel_history *h,const falafel_history_message *m)
{
    uint8_t *p=h->plain; memset(p,0,BODY);
    p[0]=m->direction; p[1]=m->delivery; p[2]=m->read; p[3]=m->text_missing;
    p[4]=m->size; p[5]=m->mail.state; p[6]=(uint8_t)(m->mail.size>>8); p[7]=(uint8_t)m->mail.size;
    p[8]=(uint8_t)(m->mail.ack_size>>8); p[9]=(uint8_t)m->mail.ack_size;
    memcpy(p+16,m->id,16); put64(p+32,m->sequence); put32(p+40,m->source); put32(p+44,m->destination);
    memcpy(p+48,m->sender,32); memcpy(p+80,m->recipient,32); memcpy(p+112,m->text,m->size);
    memcpy(p+229,m->mail.envelope,256); memcpy(p+485,m->mail.ack,256);
}
static int valid(falafel_history *h,const falafel_history_message *m)
{
    if(m->direction>1 || m->delivery>2 || m->read>1 || m->text_missing>1 || m->size>117 ||
       !m->source || !m->destination || m->source==UINT32_MAX || m->destination==UINT32_MAX ||
       m->source==m->destination || m->mail.state>2 || m->mail.size>256 || m->mail.ack_size>256 ||
       (m->text_missing && (m->direction!=FALAFEL_HISTORY_OUT || m->size))) return 0;
    const uint8_t *sender=m->direction==FALAFEL_HISTORY_OUT?h->identity->local_public:h->identity->peer_public;
    const uint8_t *recipient=m->direction==FALAFEL_HISTORY_OUT?h->identity->peer_public:h->identity->local_public;
    if(memcmp(m->sender,sender,32) || memcmp(m->recipient,recipient,32)) return 0;
    if(!m->mail.state) {
        uint8_t zero[256]={0};
        return !m->mail.size && !m->mail.ack_size && !memcmp(m->mail.envelope,zero,256) && !memcmp(m->mail.ack,zero,256);
    }
    if(m->mail.size<139 || memcmp(m->mail.envelope,"FAE\1\1",5) ||
       m->mail.size!=134u+m->mail.envelope[5] || memcmp(m->mail.envelope+70,m->id,16) ||
       memcmp(m->mail.envelope+6,m->sender,32) || memcmp(m->mail.envelope+38,m->recipient,32)) return 0;
    if(m->direction==FALAFEL_HISTORY_OUT) return m->mail.ack_size==0;
    return m->mail.ack_size==172 && !memcmp(m->mail.ack,"FAE\1\1",5) &&
        !memcmp(m->mail.ack+70,m->id,16) && !memcmp(m->mail.ack+6,m->recipient,32) &&
        !memcmp(m->mail.ack+38,m->sender,32);
}
static int read_record(falafel_history *h,unsigned direction,const uint8_t id[16],falafel_history_message *m)
{
    char name[40]; size_t n=0; filename(name,direction,id);
    if(h->storage.load(h->storage.user,name,h->wire,sizeof(h->wire),&n) || n!=800 ||
       unseal(h,"FHR1",direction,id,n)) return -1;
    const uint8_t *p=h->plain; memset(m,0,sizeof(*m));
    m->direction=p[0]; m->delivery=p[1]; m->read=p[2]; m->text_missing=p[3]; m->size=p[4];
    m->mail.state=p[5]; m->mail.size=(uint16_t)((unsigned)p[6]<<8|p[7]);
    m->mail.ack_size=(uint16_t)((unsigned)p[8]<<8|p[9]);
    memcpy(m->id,p+16,16); m->sequence=get64(p+32); m->source=get32(p+40); m->destination=get32(p+44);
    memcpy(m->sender,p+48,32); memcpy(m->recipient,p+80,32);
    memcpy(m->text,p+112,117); memcpy(m->mail.envelope,p+229,256); memcpy(m->mail.ack,p+485,256);
    falafel_crypto_secure_zero(h->plain,sizeof(h->plain));
    return !valid(h,m) || m->direction!=direction || memcmp(m->id,id,16) ? -1:0;
}
static falafel_history_result save_index(falafel_history *h)
{
    put32(h->plain,h->count);
    for(unsigned i=0;i<h->count;++i) {
        h->plain[4+i*17]=h->index[i].direction; memcpy(h->plain+5+i*17,h->index[i].id,16);
    }
    size_t n=seal(h,"FHI1",255,h->archive_id,4+h->count*17);
    if(!n || h->storage.replace(h->storage.user,"index",h->wire,n)) return poison(h,FALAFEL_HISTORY_IO);
    return FALAFEL_HISTORY_OK;
}
static int reject_record(void *context,const char *name) { (void)context; (void)name; return -1; }
static int recover_orphan(void *context,const char *name)
{
    falafel_history *h=context; uint8_t id[16]; unsigned direction=(unsigned)(name[0]-'0');
    for(unsigned i=0;i<16;++i) {
        unsigned value;
        if(sscanf(name+2+i*2,"%2x",&value)!=1) return -1;
        id[i]=(uint8_t)value;
    }
    if(find(h,direction,id)>=0) return 0;
    if(h->count==FALAFEL_HISTORY_CAPACITY) return -1;
    falafel_history_message m;
    if(read_record(h,direction,id,&m) || m.sequence!=h->count+1) return -1;
    h->index[h->count].direction=(uint8_t)direction; memcpy(h->index[h->count++].id,id,16);
    h->index[h->count-1].delivery=m.delivery; h->index[h->count-1].mail_state=m.mail.state;
    h->index[h->count-1].read=m.read;
    falafel_crypto_secure_zero(&m,sizeof(m));
    return save_index(h)!=FALAFEL_HISTORY_OK;
}
falafel_history_result falafel_history_open(falafel_history *h,falafel_history_storage storage,
    falafel_crypto_context_t *crypto,const falafel_pairing_persistent_state *identity,int create)
{
    if(!h || !crypto || !identity || !storage.load || !storage.replace || !storage.scan) return FALAFEL_HISTORY_INVALID;
    memset(h,0,sizeof(*h)); h->storage=storage; h->crypto=crypto; h->identity=identity;
    uint8_t salt[64]; memcpy(salt,identity->local_public,32); memcpy(salt+32,identity->peer_public,32);
    static const uint8_t domain[]="Falafel local history AES256GCM v1";
    if(falafel_crypto_hkdf_sha256(salt,sizeof(salt),identity->local_private,32,domain,sizeof(domain)-1,h->key,32))
        return FALAFEL_HISTORY_INVALID;
    size_t n=0; int result=storage.load(storage.user,"index",h->wire,sizeof(h->wire),&n);
    if(result==1) {
        if(!create) return FALAFEL_HISTORY_ABSENT;
        if(storage.scan(storage.user,reject_record,NULL)) return FALAFEL_HISTORY_CORRUPT;
        if(falafel_crypto_random(crypto,h->archive_id,16)) return FALAFEL_HISTORY_IO;
        h->ready=1;
        if(save_index(h)) return FALAFEL_HISTORY_IO;
    } else {
        if(result) return FALAFEL_HISTORY_IO;
        if(n<60) return FALAFEL_HISTORY_CORRUPT;
        memcpy(h->archive_id,h->wire+21,16);
        if(unseal(h,"FHI1",255,NULL,n) || n<60) return FALAFEL_HISTORY_CORRUPT;
        h->count=get32(h->plain);
        if(h->count>FALAFEL_HISTORY_CAPACITY || n!=60+h->count*17) return FALAFEL_HISTORY_CORRUPT;
        for(unsigned i=0;i<h->count;++i) {
            h->index[i].direction=h->plain[4+i*17]; memcpy(h->index[i].id,h->plain+5+i*17,16);
            for(unsigned j=0;j<i;++j) if(h->index[i].direction==h->index[j].direction &&
                !memcmp(h->index[i].id,h->index[j].id,16)) return FALAFEL_HISTORY_CORRUPT;
        }
        h->ready=1;
        for(unsigned i=0;i<h->count;++i) {
            falafel_history_message m;
            if(read_record(h,h->index[i].direction,h->index[i].id,&m) || m.sequence!=i+1)
                return poison(h,FALAFEL_HISTORY_CORRUPT);
            h->index[i].delivery=m.delivery; h->index[i].mail_state=m.mail.state; h->index[i].read=m.read;
            falafel_crypto_secure_zero(&m,sizeof(m));
        }
    }
    if(storage.scan(storage.user,recover_orphan,h)) return poison(h,FALAFEL_HISTORY_CORRUPT);
    falafel_crypto_secure_zero(h->plain,sizeof(h->plain));
    return FALAFEL_HISTORY_OK;
}
falafel_history_result falafel_history_get(falafel_history *h,unsigned direction,const uint8_t id[16],falafel_history_message *m)
{
    if(!h || !h->ready || !id || !m || direction>1) return FALAFEL_HISTORY_INVALID;
    int slot=find(h,direction,id);
    if(slot<0) return FALAFEL_HISTORY_ABSENT;
    if(read_record(h,direction,id,m) || m->sequence!=(unsigned)slot+1) return poison(h,FALAFEL_HISTORY_CORRUPT);
    h->index[slot].delivery=m->delivery; h->index[slot].mail_state=m->mail.state; h->index[slot].read=m->read;
    return FALAFEL_HISTORY_OK;
}
falafel_history_result falafel_history_at(falafel_history *h,unsigned i,falafel_history_message *m)
{
    if(!h || !h->ready || i>=h->count) return FALAFEL_HISTORY_ABSENT;
    return falafel_history_get(h,h->index[i].direction,h->index[i].id,m);
}
falafel_history_result falafel_history_put(falafel_history *h,const falafel_history_message *message)
{
    if(!h || !h->ready || !message || !valid(h,message)) return FALAFEL_HISTORY_INVALID;
    falafel_history_message m=*message,old;
    int slot=find(h,m.direction,m.id);
    if(slot>=0) {
        if(falafel_history_get(h,m.direction,m.id,&old)) return FALAFEL_HISTORY_CORRUPT;
        if(old.source!=m.source || old.destination!=m.destination || old.size!=m.size ||
           old.text_missing!=m.text_missing || memcmp(old.text,m.text,m.size) ||
           memcmp(old.sender,m.sender,32) || memcmp(old.recipient,m.recipient,32)) return FALAFEL_HISTORY_CONFLICT;
        if(old.mail.state && m.mail.state && (old.mail.size!=m.mail.size || old.mail.ack_size!=m.mail.ack_size ||
           memcmp(old.mail.envelope,m.mail.envelope,256) || memcmp(old.mail.ack,m.mail.ack,256))) return FALAFEL_HISTORY_CONFLICT;
        m.sequence=old.sequence;
        if(old.delivery>m.delivery) m.delivery=old.delivery;
        m.read=m.read||old.read;
        if(old.mail.state>=m.mail.state) m.mail=old.mail;
        uint8_t previous[BODY]; encode(h,&old); memcpy(previous,h->plain,BODY); encode(h,&m);
        if(!memcmp(previous,h->plain,BODY)) {
            falafel_crypto_secure_zero(h->plain,sizeof(h->plain));
            falafel_crypto_secure_zero(previous,sizeof(previous));
            falafel_crypto_secure_zero(&old,sizeof(old)); falafel_crypto_secure_zero(&m,sizeof(m));
            return FALAFEL_HISTORY_OK;
        }
        falafel_crypto_secure_zero(previous,sizeof(previous));
    } else {
        if(h->count==FALAFEL_HISTORY_CAPACITY) return FALAFEL_HISTORY_FULL;
        m.sequence=h->count+1; encode(h,&m);
    }
    char name[40]; filename(name,m.direction,m.id);
    size_t n=seal(h,"FHR1",m.direction,m.id,BODY);
    falafel_history_result result=FALAFEL_HISTORY_OK;
    if(!n || h->storage.replace(h->storage.user,name,h->wire,n)) result=poison(h,FALAFEL_HISTORY_IO);
    else if(slot<0) {
        h->index[h->count].direction=m.direction; memcpy(h->index[h->count++].id,m.id,16);
        result=save_index(h);
    }
    if(result==FALAFEL_HISTORY_OK) {
        unsigned position=slot<0?h->count-1:(unsigned)slot;
        h->index[position].delivery=m.delivery; h->index[position].mail_state=m.mail.state;
        h->index[position].read=m.read;
    }
    falafel_crypto_secure_zero(h->plain,sizeof(h->plain));
    falafel_crypto_secure_zero(&old,sizeof(old)); falafel_crypto_secure_zero(&m,sizeof(m));
    return result;
}
static int lookup(void *user,int outgoing,const uint8_t id[16],falafel_mail_record *record)
{
    falafel_history_message m; falafel_history *h=user;
    falafel_history_result r=falafel_history_get(h,outgoing?FALAFEL_HISTORY_OUT:FALAFEL_HISTORY_IN,id,&m);
    if(r==FALAFEL_HISTORY_ABSENT) return 1;
    if(r!=FALAFEL_HISTORY_OK) return -1;
    if(!m.mail.state) { falafel_crypto_secure_zero(&m,sizeof(m)); return 1; }
    *record=m.mail; falafel_crypto_secure_zero(&m,sizeof(m)); return 0;
}
static int retain(void *user,int outgoing,const falafel_mail_record *record)
{
    falafel_history_message m; falafel_history *h=user;
    if(record->state!=2 || falafel_history_get(h,outgoing?0:1,record->envelope+70,&m)) return -1;
    m.mail=*record; m.delivery=FALAFEL_HISTORY_ACCEPTED;
    int result=falafel_history_put(h,&m)!=FALAFEL_HISTORY_OK;
    falafel_crypto_secure_zero(&m,sizeof(m)); return result;
}
falafel_mail_retention falafel_history_retention(falafel_history *h)
{
    falafel_mail_retention retention={.user=h,.lookup=lookup,.put=retain};
    memcpy(retention.archive_id,h->archive_id,16); return retention;
}
void falafel_history_clear(falafel_history *h)
{ if(h) falafel_crypto_secure_zero(h,sizeof(*h)); }
