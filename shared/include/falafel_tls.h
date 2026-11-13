#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/* Caller owns the nonblocking fd. TLS owns only its contexts/parsed credentials.
 * All PEM buffers include the terminating NUL. No optional verification mode. */
typedef struct falafel_tls falafel_tls;
falafel_tls *falafel_tls_create(int fd, const char *server_name,
    const unsigned char *ca, size_t ca_size,
    const unsigned char *cert, size_t cert_size,
    const unsigned char *key, size_t key_size);
#ifndef ESP_PLATFORM
/* Private key must be an owner-owned regular file with mode 0600 or stricter. */
falafel_tls *falafel_tls_files(int fd, const char *server_name,
    const char *ca_path, const char *cert_path, const char *key_path);
#endif
/* 1 authenticated, 0 would block, -1 fatal. Call with a bounded deadline. */
int falafel_tls_handshake(falafel_tls *);
/* POSIX-style results; both WANT_READ and WANT_WRITE map to EAGAIN.
 * On EAGAIN, retry write with exactly the same buffer and size. */
ssize_t falafel_tls_read(void *, void *, size_t);
ssize_t falafel_tls_write(void *, const void *, size_t);
void falafel_tls_free(falafel_tls *);
