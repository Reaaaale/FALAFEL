#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "falafel_history_posix.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int valid_name(const char *name)
{
    if(!strcmp(name,"index")) return 1;
    if(strlen(name)!=38 || (name[0]!='0' && name[0]!='1') || name[1]!='-' || strcmp(name+34,".msg")) return 0;
    for(unsigned i=2;i<34;++i) if(!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f'))) return 0;
    return 1;
}
static int path(falafel_history_posix *s, const char *name)
{
    return !valid_name(name) || snprintf(s->file.path,sizeof(s->file.path),"%s/%s",s->directory,name)>=(int)sizeof(s->file.path);
}
static int load(void *user,const char *name,uint8_t *out,size_t cap,size_t *n)
{
    falafel_history_posix *s=user;
    if(path(s,name)) return -1;
    struct stat st;
    if(lstat(s->file.path,&st)) return errno==ENOENT?1:-1;
    if(!S_ISREG(st.st_mode)) return -1;
    falafel_mail_storage io=falafel_mail_posix_storage(&s->file);
    return io.load(io.user,out,cap,n);
}
static int replace(void *user,const char *name,const uint8_t *data,size_t n)
{
    falafel_history_posix *s=user;
    if(path(s,name)) return -1;
    falafel_mail_storage io=falafel_mail_posix_storage(&s->file);
    return io.replace(io.user,data,n);
}
static int scan(void *user,falafel_history_visit visit,void *context)
{
    falafel_history_posix *s=user; DIR *directory=opendir(s->directory);
    if(!directory) return -1;
    int result=0; struct dirent *entry;
    errno=0;
    while((entry=readdir(directory))) {
        if(valid_name(entry->d_name) && strcmp(entry->d_name,"index") && visit(context,entry->d_name)) { result=-1; break; }
        errno=0;
    }
    if(errno) result=-1;
    if(closedir(directory)) result=-1;
    return result;
}
int falafel_history_posix_open(falafel_history_posix *s,const char *directory,int create)
{
    if(!s || !directory || !*directory || strlen(directory)>=sizeof(s->directory)) return -1;
    char normalized[900]; strcpy(normalized,directory);
    size_t length=strlen(normalized);
    while(length>1 && normalized[length-1]=='/') normalized[--length]=0;
    directory=normalized;
    memset(s,0,sizeof(*s)); s->file.lock_fd=-1;
    struct stat st;
    if(lstat(directory,&st)) {
        if(errno!=ENOENT || !create || mkdir(directory,0700)) return -1;
        if(lstat(directory,&st)) return -1;
    }
    if(!S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077)) return -1;
    /* Also repeat after reopening: a prior mkdir may have survived an
     * interrupted/failed parent fsync. Never publish the v2 dependency first. */
    char parent[1024]; snprintf(parent,sizeof(parent),"%s",directory);
    char *slash=strrchr(parent,'/');
    if(!slash) strcpy(parent,"."); else if(slash==parent) slash[1]=0; else *slash=0;
    int fd=open(parent,O_RDONLY);
    if(fd<0) return -1;
    int result=fsync(fd); if(close(fd)) result=-1;
    if(result) return -1;
    strcpy(s->directory,directory);
    char lock[1024]; snprintf(lock,sizeof(lock),"%s/namespace",directory);
    return falafel_mail_posix_open(&s->file,lock);
}
void falafel_history_posix_close(falafel_history_posix *s)
{ if(s) falafel_mail_posix_close(&s->file); }
falafel_history_storage falafel_history_posix_storage(falafel_history_posix *s)
{ falafel_history_storage io={s,load,replace,scan}; return io; }
