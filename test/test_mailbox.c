#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "falafel_mailbox_posix.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static falafel_crypto_context_t c;
static falafel_pairing_persistent_state a,b,unknown;
static falafel_mailbox ma,mb;
static falafel_mail_posix sa,sb;
static uint8_t wire[256],ack[256],id[16]={1},text[117];
static size_t wn,an;
static char dir[128],pa[256],pb[256];
static void identity(falafel_pairing_persistent_state *p)
{
 memset(p,0,sizeof(*p)); p->has_pairing=1;p->generation=1;
 assert(!falafel_crypto_x25519_generate(&c,p->local_private,32,p->local_public,32));
}
static void openbox(falafel_mailbox *m, falafel_mail_posix *s,const char *path,
                    falafel_pairing_persistent_state *who,int create)
{
 assert(!falafel_mail_posix_open(s,path));
 assert(falafel_mail_open(m,falafel_mail_posix_storage(s),&c,who,create)==FALAFEL_MAIL_OK);
}
static void reopen(falafel_mailbox *m, falafel_mail_posix *s,const char *path,
                    falafel_pairing_persistent_state *who)
{ falafel_mail_posix_close(s); openbox(m,s,path,who,0); }
static void wait_child(pid_t p,int code)
{ int status; assert(waitpid(p,&status,0)==p); assert(WIFEXITED(status)&&WEXITSTATUS(status)==code); }
static int fail_hook(void *u,unsigned stage) { return stage==*(unsigned*)u; }
static int crash_hook(void *u,unsigned stage) { if(stage==*(unsigned*)u) _exit(77); return 0; }
static void basic(void)
{
 uint8_t again[256], got[16], out[117], bad[256], payload[38]; size_t n,m;
 openbox(&ma,&sa,pa,&a,1); openbox(&mb,&sb,pb,&b,1);
 assert(falafel_mail_enqueue(&ma,id,text,117)==FALAFEL_MAIL_OK);
 assert(falafel_mail_retry(&ma,0,wire,&wn)==FALAFEL_MAIL_OK);
 reopen(&ma,&sa,pa,&a);
 assert(falafel_mail_retry(&ma,0,again,&n)==FALAFEL_MAIL_OK && n==wn && !memcmp(wire,again,n));
 assert(falafel_mail_enqueue(&ma,id,text,117)==FALAFEL_MAIL_CONFLICT);
 assert(falafel_mail_receive(&mb,wire,wn,ack,&an)==FALAFEL_MAIL_OK && an==172);
 reopen(&mb,&sb,pb,&b);
 assert(falafel_mail_receive(&mb,wire,wn,again,&n)==FALAFEL_MAIL_DUPLICATE && n==an && !memcmp(ack,again,n));
 assert(falafel_mail_peek(&mb,got,out,&n)==FALAFEL_MAIL_OK && n==117 && !memcmp(text,out,n));
 /* Crash after external application effect but before consume: stable ID is
  * reoffered. Application's idempotent consumer keeps a durable effect ledger. */
 reopen(&mb,&sb,pb,&b);
 assert(falafel_mail_peek(&mb,got,out,&n)==FALAFEL_MAIL_OK && !memcmp(got,id,16));
 assert(falafel_mail_consume(&mb,id)==FALAFEL_MAIL_OK);
 reopen(&mb,&sb,pb,&b);
 assert(falafel_mail_peek(&mb,got,out,&n)==FALAFEL_MAIL_NOT_FOUND);
 assert(falafel_mail_consume(&mb,id)==FALAFEL_MAIL_DUPLICATE);
 assert(falafel_mail_receive(&mb,wire,wn,again,&n)==FALAFEL_MAIL_DUPLICATE);
 for(size_t i=0;i<an;++i) {
   memcpy(bad,ack,an); bad[i]^=1;
   assert(falafel_mail_ack(&ma,bad,an)!=FALAFEL_MAIL_OK);
 }
 for(size_t i=0;i<an;++i) assert(falafel_mail_ack(&ma,ack,i)!=FALAFEL_MAIL_OK);
 /* A peer-authenticated ACK for another ID, hash, label or status is rejected. */
 memcpy(payload,"FACK\1\1",6); assert(!falafel_crypto_sha256(wire,wn,payload+6));
 uint8_t other[16]={99};
 assert(!falafel_async_encrypt(&c,&b,other,payload,38,bad,256,&m));
 assert(falafel_mail_ack(&ma,bad,m)==FALAFEL_MAIL_NOT_FOUND);
 for(unsigned i=0;i<38;++i) {
   payload[i]^=1;
   assert(!falafel_async_encrypt(&c,&b,id,payload,38,bad,256,&m));
   assert(falafel_mail_ack(&ma,bad,m)==FALAFEL_MAIL_AUTH); payload[i]^=1;
 }
 assert(!falafel_async_encrypt(&c,&unknown,id,payload,38,bad,256,&m));
 assert(falafel_mail_ack(&ma,bad,m)==FALAFEL_MAIL_AUTH);
 assert(falafel_mail_ack(&ma,wire,wn)==FALAFEL_MAIL_AUTH); /* reflection/data type */
 assert(falafel_mail_receive(&ma,ack,an,bad,&m)==FALAFEL_MAIL_AUTH);
 assert(falafel_mail_ack(&ma,ack,an)==FALAFEL_MAIL_OK);
 reopen(&ma,&sa,pa,&a);
 assert(falafel_mail_ack(&ma,ack,an)==FALAFEL_MAIL_DUPLICATE);
 assert(falafel_mail_retry(&ma,0,again,&n)==FALAFEL_MAIL_NOT_FOUND);
 /* Conflicting encrypted data under the same ID cannot replace the inbox. */
 uint8_t data[6]={'F','M','S','G',1,42};
 assert(!falafel_async_encrypt(&c,&a,id,data,6,bad,256,&m));
 assert(falafel_mail_receive(&mb,bad,m,again,&n)==FALAFEL_MAIL_CONFLICT);
 assert(falafel_mail_receive(&mb,wire,1,again,&n)==FALAFEL_MAIL_AUTH);
 assert(!falafel_async_encrypt(&c,&unknown,id,data,6,bad,256,&m));
 assert(falafel_mail_receive(&mb,bad,m,again,&n)==FALAFEL_MAIL_AUTH);
 /* Independent IDs ACKed in reverse order. Retained tombstones consume slots. */
 uint8_t wires[7][256],acks[7][256]; size_t sizes[7],asizes[7];
 for(unsigned i=0;i<7;++i) {
   other[0]=(uint8_t)(i+2);
   assert(falafel_mail_enqueue(&ma,other,text,i)==FALAFEL_MAIL_OK);
   assert(falafel_mail_retry(&ma,i+1,wires[i],&sizes[i])==FALAFEL_MAIL_OK);
   assert(falafel_mail_receive(&mb,wires[i],sizes[i],acks[i],&asizes[i])==FALAFEL_MAIL_OK);
 }
 for(int i=6;i>=0;--i) assert(falafel_mail_ack(&ma,acks[i],asizes[i])==FALAFEL_MAIL_OK);
 other[0]=88;
 assert(falafel_mail_enqueue(&ma,other,text,1)==FALAFEL_MAIL_FULL);
 assert(!falafel_async_encrypt(&c,&a,other,data,6,bad,256,&m));
 assert(falafel_mail_receive(&mb,bad,m,again,&n)==FALAFEL_MAIL_FULL && n==0);
 assert(falafel_mail_receive(&mb,wire,wn,again,&n)==FALAFEL_MAIL_DUPLICATE);
 falafel_mail_posix_close(&sa); falafel_mail_posix_close(&sb);
}
/* Real process death during each atomic replacement stage for enqueue, receive,
 * ACK-state update and consumption; reload must yield old or new complete state. */
static void crash_matrix(void)
{
 for(unsigned op=0;op<4;++op) for(unsigned stage=1;stage<=7;++stage) {
   char path[256]; snprintf(path,sizeof(path),"%s/crash-%u-%u",dir,op,stage);
   falafel_pairing_persistent_state *who=(op==1||op==3)?&b:&a;
   openbox(&ma,&sa,path,who,1);
   if(op==2) assert(falafel_mail_enqueue(&ma,id,text,117)==FALAFEL_MAIL_OK);
   if(op==2) assert(falafel_mail_retry(&ma,0,wire,&wn)==FALAFEL_MAIL_OK);
   if(op==2) { /* Generate receipt for this exact envelope in a separate store. */
     char recvpath[256]; snprintf(recvpath,sizeof(recvpath),"%s/receipt-%u",dir,stage);
     openbox(&mb,&sb,recvpath,&b,1);
     assert(falafel_mail_receive(&mb,wire,wn,ack,&an)==FALAFEL_MAIL_OK);
     falafel_mail_posix_close(&sb);
   }
   if(op==3) assert(falafel_mail_receive(&ma,wire,wn,ack,&an)==FALAFEL_MAIL_OK);
   falafel_mail_posix_close(&sa);
   pid_t child=fork(); assert(child>=0);
   if(!child) {
     falafel_crypto_free(&c); assert(!falafel_crypto_init(&c));
     openbox(&ma,&sa,path,who,0); sa.hook=crash_hook; sa.hook_user=&stage;
     falafel_mail_result r;
     if(op==0) r=falafel_mail_enqueue(&ma,id,text,117);
     else if(op==1) r=falafel_mail_receive(&ma,wire,wn,ack,&an);
     else if(op==2) r=falafel_mail_ack(&ma,ack,an);
     else r=falafel_mail_consume(&ma,id);
     assert(r==FALAFEL_MAIL_OK); _exit(77); /* stage 7: after successful commit */
   }
   wait_child(child,77); openbox(&ma,&sa,path,who,0);
   int committed=stage>=5;
   if(op==0) assert(ma.outbox[0].state==(committed?1:0));
   if(op==1) {
     assert(ma.inbox[0].state==(committed?1:0));
     assert(falafel_mail_receive(&ma,wire,wn,ack,&an)==(committed?FALAFEL_MAIL_DUPLICATE:FALAFEL_MAIL_OK));
   }
   if(op==2) assert(ma.outbox[0].state==(committed?2:1));
   if(op==3) assert(ma.inbox[0].state==(committed?2:1));
   falafel_mail_posix_close(&sa);
 }
}
/* Demonstrate the required external consumer contract with a durable ID ledger:
 * effect commit, process death before consume, then idempotent recovery. */
static void consumer_crash(void)
{
 char path[256], ledger[256]; uint8_t got[16], out[117]; size_t n;
 snprintf(path,sizeof(path),"%s/consumer",dir);
 snprintf(ledger,sizeof(ledger),"%s/effect-ledger",dir);
 openbox(&ma,&sa,path,&b,1);
 assert(falafel_mail_receive(&ma,wire,wn,ack,&an)==FALAFEL_MAIL_OK);
 falafel_mail_posix_close(&sa);
 pid_t child=fork(); assert(child>=0);
 if(!child) {
   openbox(&ma,&sa,path,&b,0);
   assert(falafel_mail_peek(&ma,got,out,&n)==FALAFEL_MAIL_OK);
   int fd=open(ledger,O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd>=0);
   assert(write(fd,got,16)==16 && !fsync(fd) && !close(fd));
   _exit(78); /* effect recorded; consume not called */
 }
 wait_child(child,78); openbox(&ma,&sa,path,&b,0);
 assert(falafel_mail_peek(&ma,got,out,&n)==FALAFEL_MAIL_OK);
 uint8_t seen[16]; int fd=open(ledger,O_RDONLY); assert(fd>=0);
 assert(read(fd,seen,16)==16 && !close(fd));
 assert(!memcmp(seen,got,16)); /* consumer skips already committed effect */
 assert(falafel_mail_consume(&ma,got)==FALAFEL_MAIL_OK);
 reopen(&ma,&sa,path,&b);
 assert(falafel_mail_peek(&ma,got,out,&n)==FALAFEL_MAIL_NOT_FOUND);
 falafel_mail_posix_close(&sa);
}
static void failures(void)
{
 char path[256]; uint8_t retry[256]; size_t n;
 snprintf(path,sizeof(path),"%s/errors",dir); openbox(&ma,&sa,path,&a,1);
 falafel_mail_posix second;
 assert(falafel_mail_posix_open(&second,path)); /* exclusive writer */
 for(unsigned stage=1;stage<=6;++stage) {
   uint8_t mid[16]={(uint8_t)(stage+10)};
   sa.hook=fail_hook; sa.hook_user=&stage;
   assert(falafel_mail_enqueue(&ma,mid,text,1)==FALAFEL_MAIL_IO);
   assert(!ma.ready && falafel_mail_retry(&ma,0,retry,&n)==FALAFEL_MAIL_INVALID);
   reopen(&ma,&sa,path,&a);
   assert((ma.outbox[0].state!=0)==(stage>=5));
   /* Use separate store per failure test. */
   falafel_mail_posix_close(&sa);
   snprintf(path,sizeof(path),"%s/errors-%u",dir,stage); openbox(&ma,&sa,path,&a,1);
 }
 /* Persisted snapshots contain neither plaintext nor private keys. */
 int check=open(pa,O_RDONLY); assert(check>=0);
 uint8_t disk[FALAFEL_MAILBOX_SNAPSHOT_SIZE]; assert(read(check,disk,sizeof(disk))==(ssize_t)sizeof(disk)); close(check);
 for(size_t i=0;i+32<=sizeof(disk);++i) {
   assert(memcmp(disk+i,a.local_private,32)); assert(memcmp(disk+i,b.local_private,32));
 }
 for(size_t i=0;i+117<=sizeof(disk);++i) assert(memcmp(disk+i,text,117));
 falafel_mail_posix_close(&sa);
 /* An unavailable backend must not silently create a new store. */
 assert(falafel_mail_open(&ma,falafel_mail_posix_storage(&sa),&c,&a,1)==FALAFEL_MAIL_IO);
 /* Inbox write failure must not return an ACK or make plaintext available. */
 snprintf(path,sizeof(path),"%s/receive-failure",dir); openbox(&ma,&sa,path,&b,1);
 unsigned fault=2; sa.hook=fail_hook; sa.hook_user=&fault;
 n=99; assert(falafel_mail_receive(&ma,wire,wn,retry,&n)==FALAFEL_MAIL_IO && n==0);
 assert(!ma.ready); reopen(&ma,&sa,path,&b);
 assert(ma.inbox[0].state==0);
 assert(falafel_mail_receive(&ma,wire,wn,retry,&n)==FALAFEL_MAIL_OK);
 falafel_mail_posix_close(&sa);
 /* Corruption and truncation fail closed, without creating an empty mailbox. */
 int fd=open(path,O_WRONLY); assert(fd>=0); uint8_t bad=0x42;
 assert(pwrite(fd,&bad,1,200)==1); assert(!close(fd));
 assert(!falafel_mail_posix_open(&sa,path));
 assert(falafel_mail_open(&ma,falafel_mail_posix_storage(&sa),&c,&a,1)==FALAFEL_MAIL_CORRUPT);
 falafel_mail_posix_close(&sa);
 fd=open(path,O_WRONLY|O_TRUNC); assert(fd>=0); assert(write(fd,"FMBX",4)==4); close(fd);
 assert(!falafel_mail_posix_open(&sa,path));
 assert(falafel_mail_open(&ma,falafel_mail_posix_storage(&sa),&c,&a,1)==FALAFEL_MAIL_CORRUPT);
 falafel_mail_posix_close(&sa);
 snprintf(path,sizeof(path),"%s/missing",dir); assert(!falafel_mail_posix_open(&sa,path));
 assert(falafel_mail_open(&ma,falafel_mail_posix_storage(&sa),&c,&a,0)==FALAFEL_MAIL_NOT_FOUND);
 falafel_mail_posix_close(&sa);
 assert(falafel_mail_posix_open(&sa,"/nonexistent-falafel-directory/box"));
}
int main(void)
{
 strcpy(dir,"/tmp/falafel-mailbox-XXXXXX"); assert(mkdtemp(dir));
 snprintf(pa,sizeof(pa),"%s/a",dir); snprintf(pb,sizeof(pb),"%s/b",dir);
 assert(!falafel_crypto_init(&c)); identity(&a); identity(&b); identity(&unknown);
 memcpy(a.peer_public,b.local_public,32); memcpy(b.peer_public,a.local_public,32);
 memcpy(unknown.peer_public,a.local_public,32); memset(text,0x65,sizeof(text));
 basic(); crash_matrix(); consumer_crash(); failures();
 falafel_crypto_secure_zero(&a,sizeof(a)); falafel_crypto_secure_zero(&b,sizeof(b));
 falafel_crypto_secure_zero(&unknown,sizeof(unknown)); falafel_crypto_free(&c);
 printf("mailbox persistence and 28 process-crash scenarios: PASS (%s)\n",dir); return 0;
}
