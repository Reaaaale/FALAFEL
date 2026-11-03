#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "falafel_history_posix.h"
#include "falafel_mail_link.h"
#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static falafel_crypto_context_t crypto;
static falafel_pairing_persistent_state identities[2];
static falafel_history histories[2];
static falafel_history_posix stores[2];
static falafel_mailbox boxes[2];
static falafel_mail_posix boxstores[2];
static char root[128],base[2][256],mailpath[2][300],historypath[2][300];
static uint8_t first_wire[256],first_ack[256],first_id[16];
static size_t first_size,first_ack_size;
static void generate_identity(falafel_pairing_persistent_state *id)
{
    id->has_pairing=1; id->generation=1;
    assert(!falafel_crypto_x25519_generate(&crypto,id->local_private,32,id->local_public,32));
}
static void paths(unsigned own,const char *suffix)
{
    snprintf(base[own],sizeof(base[own]),"%s/%s-%u",root,suffix,own);
    snprintf(mailpath[own],sizeof(mailpath[own]),"%s.mail",base[own]);
    snprintf(historypath[own],sizeof(historypath[own]),"%s.history",base[own]);
}
static void open_history(unsigned own,int create)
{
    assert(!falafel_history_posix_open(&stores[own],historypath[own],create));
    assert(falafel_history_open(&histories[own],falafel_history_posix_storage(&stores[own]),&crypto,&identities[own],create)==FALAFEL_HISTORY_OK);
}
static void open_box(unsigned own,int create)
{
    assert(!falafel_mail_posix_open(&boxstores[own],mailpath[own]));
    assert(falafel_mail_open(&boxes[own],falafel_mail_posix_storage(&boxstores[own]),&crypto,&identities[own],create)==FALAFEL_MAIL_OK);
    if(boxes[own].format_version==2) {
        uint8_t id[16],text[117]; size_t n;
        assert(falafel_mail_peek(&boxes[own],id,text,&n)==FALAFEL_MAIL_INVALID);
    }
    assert(falafel_mail_set_retention(&boxes[own],falafel_history_retention(&histories[own]))==FALAFEL_MAIL_OK);
    assert(falafel_mail_upgrade_retention(&boxes[own])==FALAFEL_MAIL_OK);
}
static void close_side(unsigned own)
{ falafel_mail_posix_close(&boxstores[own]); falafel_history_posix_close(&stores[own]); falafel_history_clear(&histories[own]); }
static void reopen(unsigned own)
{ close_side(own); open_history(own,0); open_box(own,0); }
static void id_for(uint8_t id[16],unsigned n)
{ memset(id,0,16); id[0]=0xa5; id[12]=(uint8_t)(n>>24); id[13]=(uint8_t)(n>>16); id[14]=(uint8_t)(n>>8); id[15]=(uint8_t)n; }
static void message(falafel_history_message *m,unsigned own,unsigned direction,const uint8_t id[16],const uint8_t *text,size_t size)
{
    memset(m,0,sizeof(*m)); memcpy(m->id,id,16); m->direction=(uint8_t)direction;
    unsigned from=direction?1-own:own,to=1-from;
    memcpy(m->sender,identities[from].local_public,32); memcpy(m->recipient,identities[to].local_public,32);
    m->source=from+1; m->destination=to+1; m->size=(uint8_t)size; memcpy(m->text,text,size);
    m->read=direction?0:1;
}
static int slot(falafel_mail_record *records,const uint8_t id[16])
{ for(unsigned i=0;i<8;++i) if(records[i].state && !memcmp(records[i].envelope+70,id,16)) return (int)i; return -1; }
static void save_out(unsigned own,const uint8_t id[16])
{
    falafel_history_message m; assert(!falafel_history_get(&histories[own],0,id,&m));
    int i=slot(boxes[own].outbox,id); assert(i>=0); m.mail=boxes[own].outbox[i];
    m.delivery=m.mail.state==2?2:1; assert(!falafel_history_put(&histories[own],&m));
}
static void save_in(unsigned own,const uint8_t id[16])
{
    uint8_t plain[117],got[16]; size_t n=0; falafel_history_message m;
    int i=slot(boxes[own].inbox,id); assert(i>=0);
    if(boxes[own].inbox[i].state==1) {
        assert(falafel_mail_peek(&boxes[own],got,plain,&n)==FALAFEL_MAIL_OK); assert(!memcmp(got,id,16));
        message(&m,own,1,id,plain,n); m.mail=boxes[own].inbox[i]; m.delivery=2;
        assert(!falafel_history_put(&histories[own],&m));
        assert(falafel_mail_consume(&boxes[own],id)==FALAFEL_MAIL_OK);
    }
    assert(falafel_mail_reclaim(&boxes[own])==FALAFEL_MAIL_OK);
}
static void transaction(unsigned own,unsigned n)
{
    uint8_t id[16],wire[256],ack[256],again[256]; size_t wn=0,an=0,nn=0;
    id_for(id,n); const uint8_t text[]="history-private-text"; falafel_history_message m;
    message(&m,own,0,id,text,sizeof(text)-1); assert(!falafel_history_put(&histories[own],&m));
    assert(falafel_mail_enqueue(&boxes[own],id,text,sizeof(text)-1)==FALAFEL_MAIL_OK); save_out(own,id);
    int i=slot(boxes[own].outbox,id); assert(i>=0);
    assert(falafel_mail_retry(&boxes[own],(unsigned)i,wire,&wn)==FALAFEL_MAIL_OK);
    assert(falafel_mail_receive(&boxes[1-own],wire,wn,ack,&an)==FALAFEL_MAIL_OK);
    save_in(1-own,id);
    assert(falafel_mail_receive(&boxes[1-own],wire,wn,again,&nn)==FALAFEL_MAIL_DUPLICATE);
    assert(nn==an && !memcmp(ack,again,an));
    assert(falafel_mail_ack(&boxes[own],ack,an)==FALAFEL_MAIL_OK); save_out(own,id);
    assert(falafel_mail_reclaim(&boxes[own])==FALAFEL_MAIL_OK);
    assert(falafel_mail_ack(&boxes[own],ack,an)==FALAFEL_MAIL_DUPLICATE);
    assert(falafel_mail_enqueue(&boxes[own],id,text,sizeof(text)-1)==FALAFEL_MAIL_CONFLICT);
    if(n==1) { memcpy(first_wire,wire,wn); first_size=wn; memcpy(first_ack,ack,an); first_ack_size=an; memcpy(first_id,id,16); }
}
static void many_messages(void)
{
    for(unsigned own=0;own<2;++own) { paths(own,"many"); open_history(own,1); open_box(own,1); }
    for(unsigned n=1;n<=100;++n) {
        transaction(0,n); transaction(1,n+1000);
        if(n%17==0) { reopen(0); reopen(1); }
    }
    reopen(0); reopen(1);
    assert(histories[0].count==200 && histories[1].count==200);
    uint8_t ack[256]; size_t n=0;
    assert(falafel_mail_receive(&boxes[1],first_wire,first_size,ack,&n)==FALAFEL_MAIL_DUPLICATE);
    assert(n==first_ack_size && !memcmp(ack,first_ack,n));
    assert(falafel_mail_consume(&boxes[1],first_id)==FALAFEL_MAIL_DUPLICATE);
    assert(!boxes[1].inbox[0].state);
    /* A new authenticated ciphertext under an archived ID is a conflict. */
    uint8_t wire[256],plain[6]={'F','M','S','G',1,'x'}; size_t wn=0;
    assert(!falafel_async_encrypt(&crypto,&identities[0],first_id,plain,sizeof(plain),wire,sizeof(wire),&wn));
    assert(falafel_mail_receive(&boxes[1],wire,wn,ack,&n)==FALAFEL_MAIL_CONFLICT);
    falafel_history_message m; assert(!falafel_history_get(&histories[1],1,first_id,&m));
    assert(!m.read); m.read=1; assert(!falafel_history_put(&histories[1],&m));
    m.read=0; m.delivery=0; assert(!falafel_history_put(&histories[1],&m));
    reopen(1); assert(!falafel_history_get(&histories[1],1,first_id,&m)); assert(m.read && m.delivery==2);
    m.text[0]^=1; assert(falafel_history_put(&histories[1],&m)==FALAFEL_HISTORY_CONFLICT);
    /* Eight active sends still apply backpressure; retirement is not eviction. */
    const uint8_t text[]="full"; uint8_t id[16];
    for(unsigned i=0;i<9;++i) {
        id_for(id,5000+i); message(&m,0,0,id,text,4); assert(!falafel_history_put(&histories[0],&m));
        assert(falafel_mail_enqueue(&boxes[0],id,text,4)==(i<8?FALAFEL_MAIL_OK:FALAFEL_MAIL_FULL));
    }
    for(unsigned i=0;i<8;++i) assert(boxes[0].outbox[i].state==1);
    close_side(0); close_side(1);
}
static unsigned target_stage,target_write,write_count;
static int crash(void *user,unsigned stage)
{
    (void)user;
    if(stage==1) ++write_count;
    if(stage==target_stage && write_count==target_write) _exit(77);
    return 0;
}
static void wait_child(pid_t child)
{ int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77); }
static void fresh_child_crypto(void)
{ falafel_crypto_free(&crypto); assert(!falafel_crypto_init(&crypto)); }
static void record_crashes(void)
{
    for(unsigned operation=0;operation<3;++operation) for(unsigned stage=1;stage<=6;++stage) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),"crash-%u-%u",operation,stage); paths(0,suffix);
        open_history(0,1); falafel_history_message m; uint8_t id[16]; id_for(id,9000);
        const uint8_t text[]="crash-private-text"; message(&m,0,0,id,text,sizeof(text)-1);
        if(operation==2) { m.read=0; assert(!falafel_history_put(&histories[0],&m)); }
        falafel_history_posix_close(&stores[0]); falafel_history_clear(&histories[0]);
        pid_t child=fork(); assert(child>=0);
        if(!child) {
            fresh_child_crypto(); open_history(0,0);
            target_stage=stage; target_write=operation==1?2:1; write_count=0;
            stores[0].file.hook=crash;
            if(operation==2) { assert(!falafel_history_get(&histories[0],0,id,&m)); m.read=1; }
            falafel_history_put(&histories[0],&m); _exit(99);
        }
        wait_child(child); open_history(0,0);
        bool present=operation==1 || operation==2 || stage>=5;
        assert(histories[0].count==(present?1u:0u));
        if(present) {
            assert(!falafel_history_get(&histories[0],0,id,&m));
            assert(m.size==sizeof(text)-1 && !memcmp(m.text,text,m.size));
            if(operation==2) assert(m.read==(stage>=5));
        }
        falafel_history_posix_close(&stores[0]); falafel_history_clear(&histories[0]);
    }
}
static void consumer_crashes(void)
{
    for(unsigned point=0;point<5;++point) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),"consume-%u",point); paths(1,suffix);
        open_history(1,1); open_box(1,1);
        uint8_t ack[256],expected_ack[256]; size_t n=0,expected_size;
        assert(falafel_mail_receive(&boxes[1],first_wire,first_size,ack,&n)==FALAFEL_MAIL_OK);
        expected_size=n; memcpy(expected_ack,ack,n);
        close_side(1);
        pid_t child=fork(); assert(child>=0);
        if(!child) {
            fresh_child_crypto(); open_history(1,0); open_box(1,0);
            if(!point) _exit(77);
            falafel_history_message m;
            const uint8_t text[]="history-private-text";
            message(&m,1,1,first_id,text,sizeof(text)-1); m.mail=boxes[1].inbox[0]; m.delivery=2;
            assert(!falafel_history_put(&histories[1],&m));
            if(point==1) _exit(77);
            assert(falafel_mail_consume(&boxes[1],first_id)==FALAFEL_MAIL_OK);
            if(point==2) _exit(77);
            falafel_mail_retention retention=falafel_history_retention(&histories[1]);
            assert(!retention.put(retention.user,0,&boxes[1].inbox[0]));
            if(point==3) _exit(77);
            assert(falafel_mail_reclaim(&boxes[1])==FALAFEL_MAIL_OK); _exit(77);
        }
        wait_child(child); open_history(1,0); open_box(1,0);
        if(boxes[1].inbox[0].state) save_in(1,first_id);
        assert(histories[1].count==1 && !boxes[1].inbox[0].state);
        assert(falafel_mail_receive(&boxes[1],first_wire,first_size,ack,&n)==FALAFEL_MAIL_DUPLICATE);
        assert(n==expected_size && !memcmp(ack,expected_ack,n)); close_side(1);
    }
}
static void outgoing_crashes(void)
{
    for(unsigned point=0;point<6;++point) {
        char suffix[64]; snprintf(suffix,sizeof(suffix),"outgoing-%u",point); paths(0,suffix);
        open_history(0,1); open_box(0,1); close_side(0);
        uint8_t id[16]; id_for(id,10000+point);
        const uint8_t text[]="outgoing-private";
        pid_t child=fork(); assert(child>=0);
        if(!child) {
            fresh_child_crypto(); open_history(0,0); open_box(0,0);
            falafel_history_message m; message(&m,0,0,id,text,sizeof(text)-1);
            assert(!falafel_history_put(&histories[0],&m)); if(!point) _exit(77);
            assert(falafel_mail_enqueue(&boxes[0],id,text,sizeof(text)-1)==FALAFEL_MAIL_OK);
            if(point==1) _exit(77);
            save_out(0,id); if(point==2) _exit(77);
            uint8_t wire[256],ack[256]; size_t wn=0,an=0;
            assert(falafel_mail_retry(&boxes[0],0,wire,&wn)==FALAFEL_MAIL_OK);
            /* A fresh receiver exists only in this child transaction. */
            paths(1,suffix); open_history(1,1); open_box(1,1);
            assert(falafel_mail_receive(&boxes[1],wire,wn,ack,&an)==FALAFEL_MAIL_OK);
            assert(falafel_mail_ack(&boxes[0],ack,an)==FALAFEL_MAIL_OK);
            if(point==3) _exit(77);
            save_out(0,id); if(point==4) _exit(77);
            assert(falafel_mail_reclaim(&boxes[0])==FALAFEL_MAIL_OK); _exit(77);
        }
        wait_child(child); open_history(0,0); open_box(0,0);
        falafel_history_message m; assert(!falafel_history_get(&histories[0],0,id,&m));
        assert(m.size==sizeof(text)-1 && !memcmp(m.text,text,m.size));
        int i=slot(boxes[0].outbox,id);
        uint8_t exact[256]; size_t exact_size=0;
        if(i>=0) { exact_size=boxes[0].outbox[i].size; memcpy(exact,boxes[0].outbox[i].envelope,exact_size); save_out(0,id); }
        else if(!m.mail.state) {
            assert(falafel_mail_enqueue(&boxes[0],id,m.text,m.size)==FALAFEL_MAIL_OK); save_out(0,id);
        }
        if(point>=3) { assert(falafel_mail_reclaim(&boxes[0])==FALAFEL_MAIL_OK); assert(!falafel_history_get(&histories[0],0,id,&m)); assert(m.delivery==2); }
        else if(exact_size) { i=slot(boxes[0].outbox,id); assert(i>=0 && !memcmp(exact,boxes[0].outbox[i].envelope,exact_size)); }
        close_side(0);
    }
}
static void reclaim_and_migration_crashes(void)
{
    for(unsigned migration=0;migration<2;++migration) for(unsigned stage=1;stage<=6;++stage) {
        unsigned own=migration?0:1; char suffix[64];
        snprintf(suffix,sizeof(suffix),"retire-%u-%u",migration,stage); paths(own,suffix); open_history(own,1);
        uint8_t expected[256],expected_ack[256]; size_t expected_size=0,expected_ack_size=0;
        uint8_t id[16]; id_for(id,60000+stage);
        if(migration) {
            assert(!falafel_mail_posix_open(&boxstores[0],mailpath[0]));
            assert(falafel_mail_open(&boxes[0],falafel_mail_posix_storage(&boxstores[0]),&crypto,&identities[0],1)==FALAFEL_MAIL_OK);
            falafel_history_message m; message(&m,0,0,id,(const uint8_t *)"migration",9);
            assert(!falafel_history_put(&histories[0],&m));
            assert(falafel_mail_enqueue(&boxes[0],id,m.text,m.size)==FALAFEL_MAIL_OK); save_out(0,id);
            expected_size=boxes[0].outbox[0].size; memcpy(expected,boxes[0].outbox[0].envelope,expected_size);
        } else {
            open_box(1,1);
            assert(falafel_mail_receive(&boxes[1],first_wire,first_size,expected_ack,&expected_ack_size)==FALAFEL_MAIL_OK);
            falafel_history_message m; message(&m,1,1,first_id,(const uint8_t *)"history-private-text",20);
            m.delivery=2; m.mail=boxes[1].inbox[0];
            assert(!falafel_history_put(&histories[1],&m));
            assert(falafel_mail_consume(&boxes[1],first_id)==FALAFEL_MAIL_OK);
        }
        close_side(own);
        pid_t child=fork(); assert(child>=0);
        if(!child) {
            fresh_child_crypto(); open_history(own,0);
            assert(!falafel_mail_posix_open(&boxstores[own],mailpath[own]));
            assert(falafel_mail_open(&boxes[own],falafel_mail_posix_storage(&boxstores[own]),&crypto,&identities[own],0)==FALAFEL_MAIL_OK);
            assert(falafel_mail_set_retention(&boxes[own],falafel_history_retention(&histories[own]))==FALAFEL_MAIL_OK);
            target_stage=stage; target_write=1; write_count=0; boxstores[own].hook=crash;
            if(migration) falafel_mail_upgrade_retention(&boxes[own]); else falafel_mail_reclaim(&boxes[own]);
            _exit(99);
        }
        wait_child(child); open_history(own,0);
        assert(!falafel_mail_posix_open(&boxstores[own],mailpath[own]));
        assert(falafel_mail_open(&boxes[own],falafel_mail_posix_storage(&boxstores[own]),&crypto,&identities[own],0)==FALAFEL_MAIL_OK);
        if(migration) assert(boxes[0].format_version==(stage>=5?2u:1u));
        assert(falafel_mail_set_retention(&boxes[own],falafel_history_retention(&histories[own]))==FALAFEL_MAIL_OK);
        assert(falafel_mail_upgrade_retention(&boxes[own])==FALAFEL_MAIL_OK);
        if(migration) {
            uint8_t bytes[256]; size_t n=0;
            assert(falafel_mail_retry(&boxes[0],0,bytes,&n)==FALAFEL_MAIL_OK);
            assert(n==expected_size && !memcmp(bytes,expected,n));
        } else {
            assert(falafel_mail_reclaim(&boxes[1])==FALAFEL_MAIL_OK);
            uint8_t ack[256]; size_t n=0;
            assert(falafel_mail_receive(&boxes[1],first_wire,first_size,ack,&n)==FALAFEL_MAIL_DUPLICATE);
            assert(n==expected_ack_size && !memcmp(ack,expected_ack,n));
        }
        close_side(own);
    }
}
static void corruption_and_capacity(void)
{
    paths(0,"capacity"); open_history(0,1);
    uint8_t id[16]; falafel_history_message m;
    static falafel_history other; falafel_history_posix other_store; char other_path[512];
    snprintf(other_path,sizeof(other_path),"%s/other-namespace",root);
    assert(!falafel_history_posix_open(&other_store,other_path,1));
    assert(!falafel_history_open(&other,falafel_history_posix_storage(&other_store),&crypto,&identities[0],1));
    open_box(0,1);
    assert(falafel_mail_set_retention(&boxes[0],falafel_history_retention(&other))==FALAFEL_MAIL_INVALID);
    falafel_mail_posix_close(&boxstores[0]);
    falafel_history_posix_close(&other_store); falafel_history_clear(&other);
    for(unsigned i=0;i<FALAFEL_HISTORY_CAPACITY;++i) {
        id_for(id,20000+i); message(&m,0,0,id,(const uint8_t *)"secret-capacity",15);
        assert(!falafel_history_put(&histories[0],&m));
    }
    id_for(id,50000); message(&m,0,0,id,(const uint8_t *)"full",4);
    assert(falafel_history_put(&histories[0],&m)==FALAFEL_HISTORY_FULL);
    falafel_history_posix_close(&stores[0]); falafel_history_clear(&histories[0]); open_history(0,0);
    assert(histories[0].count==FALAFEL_HISTORY_CAPACITY);
    assert(!falafel_history_at(&histories[0],0,&m));
    char record_path[512]; snprintf(record_path,sizeof(record_path),"%s/0-",historypath[0]);
    size_t prefix=strlen(record_path);
    for(unsigned i=0;i<16;++i) snprintf(record_path+prefix+i*2,3,"%02x",m.id[i]);
    strcat(record_path,".msg");
    int fd=open(record_path,O_RDWR); assert(fd>=0); uint8_t saved[800]; assert(read(fd,saved,800)==800);
    assert(!memmem(saved,sizeof(saved),"secret-capacity",15));
    uint8_t bad=saved[799]^1; assert(pwrite(fd,&bad,1,799)==1 && !fsync(fd)); close(fd);
    falafel_history_posix_close(&stores[0]); falafel_history_clear(&histories[0]);
    assert(!falafel_history_posix_open(&stores[0],historypath[0],0));
    assert(falafel_history_open(&histories[0],falafel_history_posix_storage(&stores[0]),&crypto,&identities[0],0)==FALAFEL_HISTORY_CORRUPT);
    fd=open(record_path,O_WRONLY); assert(fd>=0 && write(fd,saved,800)==800 && !fsync(fd) && !close(fd));
    falafel_history_posix_close(&stores[0]);
    assert(!falafel_history_posix_open(&stores[0],historypath[0],0));
    assert(falafel_history_open(&histories[0],falafel_history_posix_storage(&stores[0]),&crypto,&identities[1],0)==FALAFEL_HISTORY_CORRUPT);
    falafel_history_posix_close(&stores[0]);
    char index_path[512]; snprintf(index_path,sizeof(index_path),"%s/index",historypath[0]);
    fd=open(index_path,O_RDWR); assert(fd>=0); assert(pread(fd,&bad,1,0)==1); bad^=1;
    assert(pwrite(fd,&bad,1,0)==1 && !fsync(fd) && !close(fd));
    assert(!falafel_history_posix_open(&stores[0],historypath[0],0));
    assert(falafel_history_open(&histories[0],falafel_history_posix_storage(&stores[0]),&crypto,&identities[0],0)==FALAFEL_HISTORY_CORRUPT);
    falafel_history_posix_close(&stores[0]); falafel_history_clear(&histories[0]);
}
int main(void)
{
    strcpy(root,"/tmp/falafel-history-XXXXXX"); assert(mkdtemp(root));
    assert(!falafel_crypto_init(&crypto)); generate_identity(&identities[0]); generate_identity(&identities[1]);
    memcpy(identities[0].peer_public,identities[1].local_public,32); memcpy(identities[1].peer_public,identities[0].local_public,32);
    many_messages(); record_crashes(); consumer_crashes(); outgoing_crashes(); reclaim_and_migration_crashes(); corruption_and_capacity();
    printf("history PASS: 100 messages each direction, 41 crash boundaries, replay/read/capacity/corruption; history RAM=%zu message=%zu record=800 index_max=%u; %s\n",
        sizeof(falafel_history),sizeof(falafel_history_message),FALAFEL_HISTORY_INDEX_BYTES,root);
    falafel_crypto_free(&crypto); return 0;
}
