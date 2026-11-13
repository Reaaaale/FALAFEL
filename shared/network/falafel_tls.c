#ifndef ESP_PLATFORM
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif
#include "falafel_tls.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>
#if defined(ESP_PLATFORM) && !defined(MBEDTLS_HAVE_TIME_DATE)
#error "mTLS requires certificate validity checks"
#endif
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct falafel_tls {
    int fd;
    bool authenticated;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca, cert;
    mbedtls_pk_context key;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_context entropy;
};
static int tls_send(void *user, const unsigned char *buf, size_t n)
{
    falafel_tls *t = user;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    ssize_t k = send(t->fd, buf, n, flags);
    if (k >= 0) return (int)k;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int tls_recv(void *user, unsigned char *buf, size_t n)
{
    falafel_tls *t = user;
    ssize_t k = recv(t->fd, buf, n, 0);
    if (k >= 0) return (int)k;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
void falafel_tls_free(falafel_tls *t)
{
    if (!t) return;
    /* Bounded teardown: do not wait for close_notify from a dead peer. */
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->config);
    mbedtls_pk_free(&t->key);
    mbedtls_x509_crt_free(&t->cert);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_ctr_drbg_free(&t->rng);
    mbedtls_entropy_free(&t->entropy);
    mbedtls_platform_zeroize(t, sizeof(*t));
    free(t);
}
falafel_tls *falafel_tls_create(int fd, const char *name,
    const unsigned char *ca, size_t ca_n, const unsigned char *cert, size_t cert_n,
    const unsigned char *key, size_t key_n)
{
    if (fd < 0 || !name || !*name || !ca || !cert || !key || !ca_n || !cert_n || !key_n)
        return NULL;
    falafel_tls *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fd = fd;
    mbedtls_ssl_init(&t->ssl); mbedtls_ssl_config_init(&t->config);
    mbedtls_x509_crt_init(&t->ca); mbedtls_x509_crt_init(&t->cert);
    mbedtls_pk_init(&t->key); mbedtls_ctr_drbg_init(&t->rng); mbedtls_entropy_init(&t->entropy);
    static const unsigned char personal[] = "falafel-mtls";
    if (mbedtls_ctr_drbg_seed(&t->rng, mbedtls_entropy_func, &t->entropy, personal, sizeof(personal)-1) ||
        mbedtls_x509_crt_parse(&t->ca, ca, ca_n) ||
        mbedtls_x509_crt_parse(&t->cert, cert, cert_n) ||
        mbedtls_pk_parse_key(&t->key, key, key_n, NULL, 0, mbedtls_ctr_drbg_random, &t->rng) ||
        mbedtls_pk_check_pair(&t->cert.pk, &t->key, mbedtls_ctr_drbg_random, &t->rng) ||
        mbedtls_ssl_config_defaults(&t->config, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) goto failed;
    mbedtls_ssl_conf_authmode(&t->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&t->config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ca_chain(&t->config, &t->ca, NULL);
    mbedtls_ssl_conf_rng(&t->config, mbedtls_ctr_drbg_random, &t->rng);
    if (mbedtls_ssl_conf_own_cert(&t->config, &t->cert, &t->key) ||
        mbedtls_ssl_setup(&t->ssl, &t->config) ||
        mbedtls_ssl_set_hostname(&t->ssl, name)) goto failed;
    mbedtls_ssl_set_bio(&t->ssl, t, tls_send, tls_recv, NULL);
    return t;
failed:
    falafel_tls_free(t);
    return NULL;
}
int falafel_tls_handshake(falafel_tls *t)
{
    if (!t) return -1;
    int r = mbedtls_ssl_handshake(&t->ssl);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if (r || mbedtls_ssl_get_verify_result(&t->ssl)) return -1;
    t->authenticated = true;
    return 1;
}
static ssize_t io_result(int n)
{
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
        errno = EAGAIN; return -1;
    }
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (n < 0) { errno = EIO; return -1; }
    return n;
}
ssize_t falafel_tls_read(void *ctx, void *buf, size_t n)
{
    falafel_tls *t = ctx;
    if (!t || !t->authenticated) { errno = ENOTCONN; return -1; }
    return io_result(mbedtls_ssl_read(&t->ssl, buf, n));
}
ssize_t falafel_tls_write(void *ctx, const void *buf, size_t n)
{
    falafel_tls *t = ctx;
    if (!t || !t->authenticated) { errno = ENOTCONN; return -1; }
    return io_result(mbedtls_ssl_write(&t->ssl, buf, n));
}
#ifndef ESP_PLATFORM
static unsigned char *read_pem(const char *path, bool private_key, size_t *size)
{
    if (!path) return NULL;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    unsigned char *buf = NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16384 ||
        (private_key && ((st.st_mode & 077) || st.st_uid != geteuid()))) goto done;
    buf = calloc(1, (size_t)st.st_size + 1);
    if (!buf) goto done;
    size_t n = 0;
    while (n < (size_t)st.st_size) {
        ssize_t k = read(fd, buf + n, (size_t)st.st_size - n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) {
            mbedtls_platform_zeroize(buf, (size_t)st.st_size + 1);
            free(buf); buf = NULL; goto done;
        }
        n += (size_t)k;
    }
    *size = n + 1;
done:
    close(fd); return buf;
}
falafel_tls *falafel_tls_files(int fd, const char *name, const char *ca_path,
    const char *cert_path, const char *key_path)
{
    size_t ca_n = 0, cert_n = 0, key_n = 0;
    unsigned char *ca = read_pem(ca_path, false, &ca_n);
    unsigned char *cert = read_pem(cert_path, false, &cert_n);
    unsigned char *key = read_pem(key_path, true, &key_n);
    falafel_tls *t = falafel_tls_create(fd, name, ca, ca_n, cert, cert_n, key, key_n);
    if (key) mbedtls_platform_zeroize(key, key_n);
    free(key); free(cert); free(ca);
    return t;
}
#endif
