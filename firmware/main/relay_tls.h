#pragma once
#include "falafel_tls.h"
#include "relay_tcp.h"
/* Opens TCP + authenticated TLS + encrypted HELLO2, bounded by 15 seconds.
 * Caller frees TLS before closing fd; NULL means fd is closed and set to -1. */
falafel_tls *falafel_relay_tls_open(const char *ipv4, uint16_t port,
    const char *server_name, uint32_t own, uint32_t peer,
    const unsigned char *ca, size_t ca_size, const unsigned char *cert, size_t cert_size,
    const unsigned char *key, size_t key_size, int *fd);
void falafel_relay_tls_attach(falafel_relay_io *, falafel_tls *);
