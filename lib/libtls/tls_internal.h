/* $OpenBSD: tls_internal.h,v 1.43 2016/09/04 12:26:43 bcook Exp $ */
/*
 * Copyright (c) 2014 Jeremie Courreges-Anglas <jca@openbsd.org>
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HEADER_TLS_INTERNAL_H
#define HEADER_TLS_INTERNAL_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include <openssl/ssl.h>

#define _PATH_SSL_CA_FILE "/etc/ssl/cert.pem"

#define TLS_CIPHERS_DEFAULT	"TLSv1.2+AEAD+ECDHE:TLSv1.2+AEAD+DHE"
#define TLS_CIPHERS_COMPAT	"HIGH:!aNULL"
#define TLS_CIPHERS_LEGACY	"HIGH:MEDIUM:!aNULL"
#define TLS_CIPHERS_ALL		"ALL:!aNULL:!eNULL"

union tls_addr {
	struct in_addr ip4;
	struct in6_addr ip6;
};

struct tls_error {
	char *msg;
	int num;
};

struct tls_keypair {
	struct tls_keypair *next;

	char *cert_mem;
	size_t cert_len;
	char *key_mem;
	size_t key_len;
};

struct tls_config {
	struct tls_error error;

	char *alpn;
	size_t alpn_len;
	const char *ca_path;
	char *ca_mem;
	size_t ca_len;
	const char *ciphers;
	int ciphers_server;
	int dheparams;
	int ecdhecurve;
	struct tls_keypair *keypair;
	uint32_t protocols;
	int verify_cert;
	int verify_client;
	int verify_depth;
	int verify_name;
	int verify_time;
};

struct tls_conninfo {
	char *alpn;
	char *cipher;
	char *servername;
	char *version;

	char *hash;
	char *issuer;
	char *subject;

	time_t notbefore;
	time_t notafter;
};

#define TLS_CLIENT		(1 << 0)
#define TLS_SERVER		(1 << 1)
#define TLS_SERVER_CONN		(1 << 2)

#define TLS_EOF_NO_CLOSE_NOTIFY	(1 << 0)
#define TLS_HANDSHAKE_COMPLETE	(1 << 1)

struct tls_sni_ctx {
	struct tls_sni_ctx *next;

	SSL_CTX *ssl_ctx;
	X509 *ssl_cert;
};

struct tls {
	struct tls_config *config;
	struct tls_error error;

	uint32_t flags;
	uint32_t state;

	char *servername;
	int socket;

	SSL *ssl_conn;
	SSL_CTX *ssl_ctx;

	struct tls_sni_ctx *sni_ctx;

	X509 *ssl_peer_cert;

	struct tls_conninfo *conninfo;

	tls_read_cb read_cb;
	tls_write_cb write_cb;
	void *cb_arg;
};

struct tls_sni_ctx *tls_sni_ctx_new(void);
void tls_sni_ctx_free(struct tls_sni_ctx *sni_ctx);

struct tls *tls_new(void);
struct tls *tls_server_conn(struct tls *ctx);

int tls_check_name(struct tls *ctx, X509 *cert, const char *servername);
int tls_configure_server(struct tls *ctx);

int tls_configure_ssl(struct tls *ctx, SSL_CTX *ssl_ctx);
int tls_configure_ssl_keypair(struct tls *ctx, SSL_CTX *ssl_ctx,
    struct tls_keypair *keypair, int required);
int tls_configure_ssl_verify(struct tls *ctx, SSL_CTX *ssl_ctx, int verify);

int tls_handshake_client(struct tls *ctx);
int tls_handshake_server(struct tls *ctx);

int tls_config_load_file(struct tls_error *error, const char *filetype,
    const char *filename, char **buf, size_t *len);
int tls_host_port(const char *hostport, char **host, char **port);

int tls_set_cbs(struct tls *ctx,
    tls_read_cb read_cb, tls_write_cb write_cb, void *cb_arg);

int tls_error_set(struct tls_error *error, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
int tls_error_setx(struct tls_error *error, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
int tls_config_set_error(struct tls_config *cfg, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
int tls_config_set_errorx(struct tls_config *cfg, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
int tls_set_error(struct tls *ctx, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
int tls_set_errorx(struct tls *ctx, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));

int tls_ssl_error(struct tls *ctx, SSL *ssl_conn, int ssl_ret,
    const char *prefix);

int tls_conninfo_populate(struct tls *ctx);
void tls_conninfo_free(struct tls_conninfo *conninfo);

int asn1_time_parse(const char *, size_t, struct tm *, int);

#endif /* HEADER_TLS_INTERNAL_H */
