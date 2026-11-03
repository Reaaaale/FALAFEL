#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "falafel_mailbox_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static int parent_sync(const char *path)
{
    char parent[1024], *slash; int fd, rc;
    strcpy(parent,path); slash=strrchr(parent,'/');
    if(!slash) strcpy(parent,".");
    else if(slash==parent) slash[1]=0; else *slash=0;
    fd=open(parent,O_RDONLY); if(fd<0) return -1;
    rc=fsync(fd); if(close(fd)) rc=-1; return rc;
}
static int load(void *u, uint8_t *out, size_t cap, size_t *n)
{
    falafel_mail_posix *s=u; int fd; size_t total=0; uint8_t extra;
    if(s->lock_fd<0) return -1;
    fd=open(s->path,O_RDONLY); if(fd<0) return errno==ENOENT?1:-1;
    while(total<cap) {
        ssize_t count=read(fd,out+total,cap-total);
        if(count<0 && errno==EINTR) continue;
        if(count<0) { close(fd); return -1; }
        if(!count) break;
        total+=(size_t)count;
    }
    ssize_t more; do { more=read(fd,&extra,1); } while(more<0 && errno==EINTR);
    /* Reestablish durability of a surviving rename after an interrupted save. */
    int rc=(more!=0 || fsync(fd))?-1:0;
    if(close(fd)) rc=-1;
    if(!rc && parent_sync(s->path)) rc=-1;
    *n=total; return rc;
}
static int write_all(int fd, const uint8_t *p, size_t n)
{
    while(n) { ssize_t k=write(fd,p,n); if(k<0 && errno==EINTR) continue;
        if(k<=0) return -1;
        p+=(size_t)k; n-=(size_t)k;
    } return 0;
}
static int stage(falafel_mail_posix *s, unsigned n)
{ return s->hook?s->hook(s->hook_user,n):0; }
static int replace(void *u, const uint8_t *p, size_t n)
{
    falafel_mail_posix *s=u; char tmp[1050]; int fd, rc=-1;
    if(s->lock_fd<0 || snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",s->path)>=(int)sizeof(tmp)) return -1;
    fd=mkstemp(tmp); if(fd<0) return -1;
    if(fchmod(fd,0600) || stage(s,1) || write_all(fd,p,n/2) || stage(s,2) ||
       write_all(fd,p+n/2,n-n/2) || stage(s,3) || fsync(fd) || stage(s,4)) goto done;
    if(close(fd)) { fd=-1; goto done; } fd=-1;
    if(rename(tmp,s->path) || stage(s,5) || parent_sync(s->path) || stage(s,6)) goto done;
    rc=0;
done:
    if(fd>=0) close(fd);
    unlink(tmp); return rc;
}
int falafel_mail_posix_open(falafel_mail_posix *s, const char *path)
{
    char lock[1040];
    if(!s || !path || strlen(path)>=sizeof(s->path)) return -1;
    memset(s,0,sizeof(*s)); s->lock_fd=-1; strcpy(s->path,path);
    if(snprintf(lock,sizeof(lock),"%s.lock",path)>=(int)sizeof(lock)) return -1;
    s->lock_fd=open(lock,O_CREAT|O_RDWR,0600);
    if(s->lock_fd<0) return -1;
    if(flock(s->lock_fd,LOCK_EX|LOCK_NB)) { close(s->lock_fd); s->lock_fd=-1; return -1; }
    return 0;
}
void falafel_mail_posix_close(falafel_mail_posix *s)
{ if(s && s->lock_fd>=0) { close(s->lock_fd); s->lock_fd=-1; } }
falafel_mail_storage falafel_mail_posix_storage(falafel_mail_posix *s)
{ falafel_mail_storage io={s,load,replace}; return io; }
