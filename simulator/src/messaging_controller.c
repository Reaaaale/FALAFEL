#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "messaging_controller.h"
#include "desktop_transport.h"
#include "falafel_mail_link.h"
#include "falafel_mailbox_posix.h"
#include "falafel_history_posix.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct { uint64_t request; size_t size; char text[118]; bool mark_read; } command;
struct messaging_controller {
    pthread_t thread;
    atomic_bool stop, done, accepting, failed;
    atomic_uint command_head, command_tail, event_head, event_tail;
    command commands[MESSAGING_COMMAND_CAPACITY];
    messaging_event events[MESSAGING_EVENT_CAPACITY];
    uint64_t next_request;
    char host[128], identity_path[1024], mailbox_path[1024], history_path[900];
    bool create_history, full_reported;
    unsigned pump_cursor;
    uint16_t port;
    uint32_t own, peer;
    /* Exclusively worker-owned below. */
    falafel_crypto_context_t crypto;
    falafel_pairing_persistent_state identity;
    falafel_mailbox mailbox;
    falafel_mail_posix store;
    falafel_mail_link link;
    desktop_transport_t transport;
    falafel_history *history;
    falafel_history_posix history_store;
    uint8_t out_state[8];
};

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void pause_worker(void)
{
    struct timespec t = {0, 2000000};
    nanosleep(&t, NULL);
}

/* Strict UTF-8 without embedded NUL; mailbox payloads themselves stay binary. */
bool messaging_valid_text(const char *text, size_t n)
{
    if (!text || !n || n > MESSAGING_TEXT_MAX) return false;
    for (size_t i = 0; i < n;) {
        uint8_t c = (uint8_t)text[i++];
        if (!c) return false;
        if (c < 0x80) continue;
        unsigned count; uint32_t value, minimum;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (i + count > n) return false;
        while (count--) {
            c = (uint8_t)text[i++];
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

/* SPSC rings: publish after copying, release a slot only after copying/erasing.
 * GUI calls do not acquire a mutex or wait for the worker. */
bool messaging_try_send(messaging_controller *c, const char *text, size_t n, uint64_t *request)
{
    if (!c || !request || !messaging_valid_text(text, n) ||
        !atomic_load_explicit(&c->accepting, memory_order_acquire) ||
        atomic_load_explicit(&c->stop, memory_order_acquire)) return false;
    unsigned h = atomic_load_explicit(&c->command_head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&c->command_tail, memory_order_acquire);
    if (h - t == MESSAGING_COMMAND_CAPACITY || c->next_request == UINT64_MAX) return false;
    command *cmd = &c->commands[h % MESSAGING_COMMAND_CAPACITY];
    cmd->mark_read = false; cmd->request = ++c->next_request; cmd->size = n;
    memcpy(cmd->text, text, n); cmd->text[n] = 0; *request = cmd->request;
    atomic_store_explicit(&c->command_head, h + 1, memory_order_release);
    return true;
}
bool messaging_try_mark_read(messaging_controller *c)
{
    if (!c || !atomic_load_explicit(&c->accepting, memory_order_acquire) || atomic_load(&c->stop)) return false;
    unsigned h=atomic_load_explicit(&c->command_head,memory_order_relaxed);
    unsigned t=atomic_load_explicit(&c->command_tail,memory_order_acquire);
    if(h-t==MESSAGING_COMMAND_CAPACITY) return false;
    command *cmd=&c->commands[h%MESSAGING_COMMAND_CAPACITY]; memset(cmd,0,sizeof(*cmd)); cmd->mark_read=true;
    atomic_store_explicit(&c->command_head,h+1,memory_order_release); return true;
}
static bool pop_command(messaging_controller *c, command *cmd)
{
    unsigned t = atomic_load_explicit(&c->command_tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&c->command_head, memory_order_acquire)) return false;
    *cmd = c->commands[t % MESSAGING_COMMAND_CAPACITY];
    falafel_crypto_secure_zero(&c->commands[t % MESSAGING_COMMAND_CAPACITY], sizeof(*cmd));
    atomic_store_explicit(&c->command_tail, t + 1, memory_order_release);
    return true;
}
bool messaging_try_event(messaging_controller *c, messaging_event *event)
{
    if (!c || !event) return false;
    unsigned t = atomic_load_explicit(&c->event_tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&c->event_head, memory_order_acquire)) return false;
    *event = c->events[t % MESSAGING_EVENT_CAPACITY];
    /* No crypto-provider use on the GUI thread. */
    memset(&c->events[t % MESSAGING_EVENT_CAPACITY], 0, sizeof(*event));
    atomic_store_explicit(&c->event_tail, t + 1, memory_order_release);
    return true;
}
static bool push_event(messaging_controller *c, const messaging_event *event)
{
    for (;;) {
        unsigned h = atomic_load_explicit(&c->event_head, memory_order_relaxed);
        unsigned t = atomic_load_explicit(&c->event_tail, memory_order_acquire);
        if (h - t < MESSAGING_EVENT_CAPACITY) {
            c->events[h % MESSAGING_EVENT_CAPACITY] = *event;
            atomic_store_explicit(&c->event_head, h + 1, memory_order_release);
            return true;
        }
        /* Backpressure stalls only this worker. Accepted inbox stays durable.
         * Shutdown may discard presentation events, never durable mail records. */
        if (atomic_load_explicit(&c->stop, memory_order_acquire)) return false;
        pause_worker();
    }
}
static void report(messaging_controller *c, messaging_error code, bool fatal, uint64_t request)
{
    if (fatal) {
        atomic_store_explicit(&c->failed, true, memory_order_release);
        atomic_store_explicit(&c->accepting, false, memory_order_release);
    }
    messaging_event event = {.kind = MESSAGING_ERROR, .error = code,
                             .fatal = fatal, .request = request};
    push_event(c, &event);
}
static messaging_error error_code(falafel_mail_result r)
{
    if (r == FALAFEL_MAIL_FULL) return MESSAGING_FULL;
    if (r == FALAFEL_MAIL_IO || r == FALAFEL_MAIL_CORRUPT) return MESSAGING_STORAGE_ERROR;
    if (r == FALAFEL_MAIL_AUTH) return MESSAGING_AUTH_ERROR;
    return MESSAGING_INTERNAL_ERROR;
}
static int send_frame(void *user, const uint8_t *frame, size_t size)
{ return desktop_transport_send_frame(user, frame, size); }
static bool receive_frame(void *user, const uint8_t *frame, size_t size)
{
    messaging_controller *c = user;
    falafel_mail_frame parsed;
    if (falafel_mail_frame_decode(frame, size, &parsed)) return false;
    falafel_mail_result r = falafel_mail_link_receive(&c->link, frame, size);
    if (r != FALAFEL_MAIL_OK && r != FALAFEL_MAIL_DUPLICATE)
        report(c, error_code(r), !c->mailbox.ready, 0);
    return c->mailbox.ready != 0;
}

static bool load_identity(messaging_controller *c)
{
    uint8_t bytes[65] = {0}; struct stat st;
    int fd = open(c->identity_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    bool good = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size == 64 &&
                !(st.st_mode & 077) && st.st_uid == geteuid();
    size_t n = 0;
    while (good && n < sizeof(bytes)) {
        ssize_t k = read(fd, bytes + n, sizeof(bytes) - n);
        if (k < 0) { good = false; break; }
        if (!k) break;
        n += (size_t)k;
    }
    if (close(fd)) good = false;
    good = good && n == 64;
    if (good) {
        memcpy(c->identity.local_private, bytes, 32);
        memcpy(c->identity.peer_public, bytes + 32, 32);
        c->identity.has_pairing = 1; c->identity.generation = 1;
        good = falafel_crypto_x25519_public_from_private(&c->crypto,
            c->identity.local_private, 32, c->identity.local_public, 32) == FALAFEL_CRYPTO_OK;
    }
    falafel_crypto_secure_zero(bytes, sizeof(bytes));
    return good;
}

static void record_id(uint8_t id[16], const falafel_mail_record *r)
{
    /* Existing validated envelope header ends with its opaque application ID. */
    memcpy(id, r->envelope + FALAFEL_ASYNC_HEADER_SIZE - 16, 16);
}
static bool history_failure(messaging_controller *c,falafel_history_result result,uint64_t request)
{
    bool full=result==FALAFEL_HISTORY_FULL;
    if(!full || !c->full_reported || request) report(c,full?MESSAGING_FULL:MESSAGING_STORAGE_ERROR,!full,request);
    if(full) c->full_reported=true;
    return full;
}
static void message_base(messaging_controller *c,falafel_history_message *m,unsigned direction,const uint8_t id[16])
{
    memset(m,0,sizeof(*m)); memcpy(m->id,id,16); m->direction=(uint8_t)direction;
    bool sent=direction==FALAFEL_HISTORY_OUT;
    memcpy(m->sender,sent?c->identity.local_public:c->identity.peer_public,32);
    memcpy(m->recipient,sent?c->identity.peer_public:c->identity.local_public,32);
    m->source=sent?c->own:c->peer; m->destination=sent?c->peer:c->own;
}
static bool publish_message(messaging_controller *c,unsigned direction,const uint8_t id[16],
    messaging_event_kind kind,bool restored,uint64_t request)
{
    falafel_history_message m;
    if(falafel_history_get(c->history,direction,id,&m)) { report(c,MESSAGING_STORAGE_ERROR,true,request); return false; }
    messaging_event event={.kind=kind,.restored=restored,.request=request,
        .sent=direction==FALAFEL_HISTORY_OUT,.delivery=m.delivery,.read=m.read!=0};
    memcpy(event.id,id,16);
    if(m.text_missing) strcpy(event.text,"[Testo precedente non disponibile]");
    else if(messaging_valid_text((const char *)m.text,m.size)) { memcpy(event.text,m.text,m.size); event.text[m.size]=0; }
    else strcpy(event.text,"[Messaggio non testuale]");
    bool result=push_event(c,&event);
    falafel_crypto_secure_zero(&event,sizeof(event)); falafel_crypto_secure_zero(&m,sizeof(m));
    return result;
}
static bool sync_active(messaging_controller *c,bool notify,bool migrate)
{
    for(unsigned i=0;i<8;++i) {
        falafel_mail_record *out=&c->mailbox.outbox[i];
        if(out->state && (migrate || c->out_state[i]!=out->state)) {
            uint8_t id[16]; record_id(id,out); falafel_history_message m;
            falafel_history_result r=falafel_history_get(c->history,0,id,&m);
            if(r==FALAFEL_HISTORY_ABSENT && migrate) {
                message_base(c,&m,0,id); m.text_missing=1; m.read=1;
            } else if(r!=FALAFEL_HISTORY_OK) { report(c,MESSAGING_STORAGE_ERROR,true,0); return false; }
            unsigned previous=m.delivery;
            m.mail=*out; m.delivery=out->state==2?FALAFEL_HISTORY_ACCEPTED:FALAFEL_HISTORY_QUEUED;
            r=falafel_history_put(c->history,&m);
            falafel_crypto_secure_zero(&m,sizeof(m));
            if(r) return history_failure(c,r,0);
            c->out_state[i]=out->state;
            if(notify && out->state==2 && previous!=FALAFEL_HISTORY_ACCEPTED)
                publish_message(c,0,id,MESSAGING_DELIVERED,false,0);
        }
        falafel_mail_record *in=&c->mailbox.inbox[i];
        if(!in->state) continue;
        uint8_t plain[122],id[16]; size_t size=0;
        if(falafel_async_decrypt(&c->crypto,&c->identity,in->envelope,in->size,
            plain,sizeof(plain),&size,id) || size<5 || memcmp(plain,"FMSG\1",5)) {
            falafel_crypto_secure_zero(plain,sizeof(plain)); report(c,MESSAGING_AUTH_ERROR,true,0); return false;
        }
        falafel_history_message m,old; message_base(c,&m,1,id);
        falafel_history_result existed=falafel_history_get(c->history,1,id,&old);
        if(existed!=FALAFEL_HISTORY_OK && existed!=FALAFEL_HISTORY_ABSENT) {
            report(c,MESSAGING_STORAGE_ERROR,true,0); return false;
        }
        m.size=(uint8_t)(size-5); memcpy(m.text,plain+5,m.size); m.mail=*in; m.delivery=FALAFEL_HISTORY_ACCEPTED;
        falafel_crypto_secure_zero(plain,sizeof(plain)); falafel_crypto_secure_zero(&old,sizeof(old));
        falafel_history_result r=falafel_history_put(c->history,&m);
        falafel_crypto_secure_zero(&m,sizeof(m));
        if(r) return history_failure(c,r,0);
        /* The durable, idempotent application effect is now committed. */
        if(in->state==1 && falafel_mail_consume(&c->mailbox,id)!=FALAFEL_MAIL_OK) {
            report(c,MESSAGING_STORAGE_ERROR,true,0); return false;
        }
        if(notify && existed==FALAFEL_HISTORY_ABSENT) publish_message(c,1,id,MESSAGING_RECEIVED,false,0);
    }
    if(c->mailbox.retention.put && falafel_mail_link_reclaim(&c->link)!=FALAFEL_MAIL_OK) {
        report(c,MESSAGING_STORAGE_ERROR,true,0); return false;
    }
    for(unsigned i=0;i<8;++i) if(!c->mailbox.outbox[i].state) c->out_state[i]=0;
    return true;
}
static bool pump_intent(messaging_controller *c)
{
    bool vacant=false;
    for(unsigned i=0;i<8;++i) if(!c->mailbox.outbox[i].state) vacant=true;
    if(!vacant) return true;
    for(unsigned step=0;step<c->history->count;++step) {
        unsigned index=c->pump_cursor++%c->history->count;
        falafel_history_key *key=&c->history->index[index];
        if(key->direction!=0 || key->mail_state || key->delivery!=FALAFEL_HISTORY_PENDING) continue;
        falafel_history_message m;
        if(falafel_history_at(c->history,index,&m)) { report(c,MESSAGING_STORAGE_ERROR,true,0); return false; }
        falafel_mail_result result=falafel_mail_enqueue(&c->mailbox,m.id,m.text,m.size);
        if(result!=FALAFEL_MAIL_OK) { falafel_crypto_secure_zero(&m,sizeof(m)); report(c,error_code(result),true,0); return false; }
        for(unsigned i=0;i<8;++i) if(c->mailbox.outbox[i].state && !memcmp(c->mailbox.outbox[i].envelope+70,m.id,16)) {
            m.mail=c->mailbox.outbox[i]; c->out_state[i]=1; break;
        }
        m.delivery=FALAFEL_HISTORY_QUEUED;
        falafel_history_result r=falafel_history_put(c->history,&m);
        if(!r) publish_message(c,0,m.id,MESSAGING_HISTORY,false,0);
        falafel_crypto_secure_zero(&m,sizeof(m));
        if(r) { history_failure(c,r,0); return false; }
        return true;
    }
    return true;
}
static bool process_command(messaging_controller *c,const command *cmd)
{
    if(cmd->mark_read) {
        for(unsigned i=0;i<c->history->count;++i) if(c->history->index[i].direction==1 && !c->history->index[i].read) {
            falafel_history_message m;
            if(falafel_history_at(c->history,i,&m)) { report(c,MESSAGING_STORAGE_ERROR,true,0); return false; }
            m.read=1;
            falafel_history_result r=falafel_history_put(c->history,&m);
            if(!r) publish_message(c,1,m.id,MESSAGING_HISTORY,false,0);
            falafel_crypto_secure_zero(&m,sizeof(m));
            if(r) { history_failure(c,r,0); return false; }
        }
        return true;
    }
    uint8_t id[16];
    if(falafel_crypto_random(&c->crypto,id,16)) { report(c,MESSAGING_AUTH_ERROR,false,cmd->request); return true; }
    falafel_history_message m; message_base(c,&m,0,id);
    memcpy(m.text,cmd->text,cmd->size); m.size=(uint8_t)cmd->size; m.read=1;
    falafel_history_result r=falafel_history_put(c->history,&m);
    falafel_crypto_secure_zero(&m,sizeof(m));
    if(r) return history_failure(c,r,cmd->request);
    publish_message(c,0,id,MESSAGING_STORED,false,cmd->request);
    return true;
}
static void *worker(void *user)
{
    messaging_controller *c=user; bool crypto_open=false,transport_open=false;
    c->store.lock_fd=-1; c->history_store.file.lock_fd=-1;
    if(falafel_crypto_init(&c->crypto)) { report(c,MESSAGING_INTERNAL_ERROR,true,0); goto done; }
    crypto_open=true;
    if(!load_identity(c)) { report(c,MESSAGING_IDENTITY_ERROR,true,0); goto done; }
    struct stat mailbox_stat;
    if(lstat(c->mailbox_path,&mailbox_stat) || !S_ISREG(mailbox_stat.st_mode) ||
        falafel_mail_posix_open(&c->store,c->mailbox_path) ||
        falafel_mail_open(&c->mailbox,falafel_mail_posix_storage(&c->store),&c->crypto,&c->identity,0)) {
        report(c,MESSAGING_STORAGE_ERROR,true,0); goto done;
    }
    c->history=calloc(1,sizeof(*c->history));
    if(!c->history || falafel_history_posix_open(&c->history_store,c->history_path,c->create_history && c->mailbox.format_version==1) ||
       falafel_history_open(c->history,falafel_history_posix_storage(&c->history_store),
           &c->crypto,&c->identity,c->create_history && c->mailbox.format_version==1)) {
        report(c,MESSAGING_STORAGE_ERROR,true,0); goto done;
    }
    desktop_transport_init_frames(&c->transport,c->host,c->port,c->own,c->peer,receive_frame,c);
    transport_open=true;
    falafel_mail_link_init(&c->link,&c->mailbox,c->own,c->peer,send_frame,&c->transport);
    bool legacy=c->mailbox.format_version==1;
    if(!legacy && falafel_mail_set_retention(&c->mailbox,falafel_history_retention(c->history))) {
        report(c,MESSAGING_STORAGE_ERROR,true,0); goto done;
    }
    if(!sync_active(c,false,legacy) ||
        falafel_mail_set_retention(&c->mailbox,falafel_history_retention(c->history)) ||
        falafel_mail_upgrade_retention(&c->mailbox) || !sync_active(c,false,false)) {
        report(c,MESSAGING_STORAGE_ERROR,true,0); goto done;
    }
    atomic_store_explicit(&c->accepting,true,memory_order_release);
    messaging_event ready={.kind=MESSAGING_READY}; push_event(c,&ready);
    for(unsigned i=0;i<c->history->count;++i) {
        falafel_history_key *key=&c->history->index[i];
        if(!publish_message(c,key->direction,key->id,MESSAGING_HISTORY,true,0)) break;
    }
    bool connected=false;
    while(c->mailbox.ready && c->history->ready && !atomic_load(&c->failed)) {
        command cmd;
        if(pop_command(c,&cmd)) {
            bool result=process_command(c,&cmd); falafel_crypto_secure_zero(&cmd,sizeof(cmd));
            if(!result) break;
        }
        if(atomic_load_explicit(&c->stop,memory_order_acquire)) {
            if(atomic_load_explicit(&c->command_head,memory_order_acquire)==atomic_load(&c->command_tail)) break;
            continue;
        }
        if(!sync_active(c,true,false) || !pump_intent(c)) break;
        desktop_transport_poll(&c->transport);
        bool current=desktop_transport_is_connected(&c->transport);
        if(current!=connected) {
            if(current) falafel_mail_link_connected(&c->link);
            connected=current;
            messaging_event event={.kind=MESSAGING_CONNECTION,.connected=current}; push_event(c,&event);
        }
        if(current && c->mailbox.ready) falafel_mail_link_tick(&c->link,now_ms());
        pause_worker();
    }
 done:
    atomic_store_explicit(&c->accepting,false,memory_order_release);
    command rejected;
    while(pop_command(c,&rejected)) {
        report(c,MESSAGING_STORAGE_ERROR,true,rejected.request); falafel_crypto_secure_zero(&rejected,sizeof(rejected));
    }
    if(transport_open) desktop_transport_deinit(&c->transport);
    falafel_history_posix_close(&c->history_store);
    if(c->history) { falafel_history_clear(c->history); free(c->history); c->history=NULL; }
    falafel_mail_posix_close(&c->store);
    falafel_crypto_secure_zero(&c->identity,sizeof(c->identity)); falafel_crypto_secure_zero(&c->mailbox,sizeof(c->mailbox));
    if(crypto_open) falafel_crypto_free(&c->crypto);
    atomic_store_explicit(&c->done,true,memory_order_release); return NULL;
}

messaging_controller *messaging_start(const messaging_config *config)
{
    if (!config || !config->host || !config->identity_path || !config->mailbox_path ||
        !config->own || !config->peer || config->own == config->peer ||
        config->own == UINT32_MAX || config->peer == UINT32_MAX || !config->port ||
        !*config->host || !*config->identity_path || !*config->mailbox_path ||
        strlen(config->host) >= 128 || strlen(config->identity_path) >= 1024 ||
        strlen(config->mailbox_path) >= 850 ||
        (config->history_path && strlen(config->history_path)>=900)) return NULL;
    messaging_controller *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    atomic_init(&c->stop, false); atomic_init(&c->done, false); atomic_init(&c->accepting, false);
    atomic_init(&c->failed, false);
    atomic_init(&c->command_head, 0); atomic_init(&c->command_tail, 0);
    atomic_init(&c->event_head, 0); atomic_init(&c->event_tail, 0);
    strcpy(c->host, config->host); strcpy(c->identity_path, config->identity_path);
    strcpy(c->mailbox_path, config->mailbox_path);
    if(config->history_path) strcpy(c->history_path,config->history_path);
    else snprintf(c->history_path,sizeof(c->history_path),"%s.history",config->mailbox_path);
    c->create_history=config->create_history;
    c->port = config->port; c->own = config->own; c->peer = config->peer;
    if (pthread_create(&c->thread, NULL, worker, c)) { free(c); return NULL; }
    return c;
}
void messaging_request_stop(messaging_controller *c)
{
    if (c) atomic_store_explicit(&c->stop, true, memory_order_release);
}
bool messaging_finished(const messaging_controller *c)
{ return !c || atomic_load_explicit(&c->done, memory_order_acquire); }
bool messaging_failed(const messaging_controller *c)
{ return c && atomic_load_explicit(&c->failed, memory_order_acquire); }
size_t messaging_static_memory(void) { return sizeof(messaging_controller)+sizeof(falafel_history); }
void messaging_destroy(messaging_controller *c)
{
    if (!c || !messaging_finished(c)) return;
    pthread_join(c->thread, NULL);
    falafel_crypto_secure_zero(c, sizeof(*c)); free(c);
}
