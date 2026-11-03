#pragma once
#include "falafel_history.h"
#include "falafel_mailbox_posix.h"
typedef struct {
    char directory[900];
    falafel_mail_posix file; /* One exclusive namespace lock; atomic-file I/O reused. */
} falafel_history_posix;
int falafel_history_posix_open(falafel_history_posix *, const char *, int create);
void falafel_history_posix_close(falafel_history_posix *);
falafel_history_storage falafel_history_posix_storage(falafel_history_posix *);
