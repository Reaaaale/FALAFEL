#ifndef FALAFEL_MAILBOX_POSIX_H
#define FALAFEL_MAILBOX_POSIX_H
#include "falafel_mailbox.h"
/* Hook is for deterministic failure/crash tests. Nonzero aborts save; it may
 * already have committed. Stages: 1 before write, 2 partial write, 3 full write,
 * 4 file fsync, 5 rename, 6 directory fsync. NULL disables injection. */
typedef struct {
    char path[1024];
    int lock_fd;
    int (*hook)(void *, unsigned);
    void *hook_user;
} falafel_mail_posix;
int falafel_mail_posix_open(falafel_mail_posix *, const char *path);
void falafel_mail_posix_close(falafel_mail_posix *);
falafel_mail_storage falafel_mail_posix_storage(falafel_mail_posix *);
#endif
