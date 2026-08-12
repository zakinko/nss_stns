/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 zakinko
 *
 * nss_stns - STNS name service switch module for NetBSD.
 *
 * Portions are derived from libnss (https://github.com/STNS/libnss),
 * Copyright (c) 2026 pyama86, distributed under the MIT license.
 * See LICENSE for the full text of both licenses.
 */
#ifndef STNS_H
#define STNS_H

#include <sys/types.h>
#include <sys/stat.h>

#include <grp.h>
#include <nsswitch.h>
#include <pthread.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "parson.h"
#include "toml.h"

/*
 * This is the nsswitch(5) module interface NetBSD designed: libc dlopen()s
 * nss_<source>.so.<NSS_MODULE_INTERFACE_VERSION> and calls the
 * nss_module_register() it finds there.
 */
#if !defined(__NetBSD__)
#error "nss_stns is a NetBSD nsswitch module"
#endif
#define STNS_NSS_NETBSD 1

#define STNS_VERSION "0.1.0"
#define STNS_USER_AGENT "nss_stns/" STNS_VERSION

/*
 * Where the client configuration lives.  STNS_CONFDIR is overridable at build
 * time; the Makefile points it at pkgsrc's LOCALBASE, /usr/pkg.  /etc/stns/client/stns.conf is honoured as a fallback so that a
 * configuration copied verbatim from a Linux host keeps working.
 */
#ifndef STNS_CONFDIR
#define STNS_CONFDIR "/usr/pkg/etc"
#endif

/*
 * Search order for the client configuration.  Both entries keep upstream
 * STNS's stns/client layout rather than flattening to a bare stns.conf: the
 * server's configuration is stns/server/stns.conf, so a flat name would become
 * ambiguous the day an STNS server is packaged for these systems too.  The
 * second entry is where a Linux host keeps the file, honoured so that a
 * stns.conf can be copied over unchanged.
 *
 * Note that /etc/nsswitch.conf is not ours to place: it belongs to the base
 * system, and its path is fixed as _PATH_NS_CONF in <nsswitch.h>.
 */
#define STNS_CONFIG_FILE STNS_CONFDIR "/stns/client/stns.conf"
#define STNS_CONFIG_FILE_COMPAT "/etc/stns/client/stns.conf"

#define STNS_DEFAULT_API_ENDPOINT "http://localhost:1104/v1"

/*
 * Where cached responses go by default.  NetBSD's hier(7) has no /var/cache;
 * automatically generated data belongs under /var/db.  An explicit cache_dir
 * in stns.conf always wins, so a configuration copied from a Linux host still
 * lands where it says it does.
 */
#define STNS_DEFAULT_CACHE_DIR "/var/db/stns"
#define STNS_DEFAULT_CACHED_SOCKET "/var/run/cache-stnsd.sock"
#define STNS_DEFAULT_SHELL "/bin/sh"
#define STNS_DEFAULT_HOME_PREFIX "/home"

/* 10MB */
#define STNS_MAX_BUFFER_SIZE (10 * 1024 * 1024)
#define STNS_DEFAULT_BUFFER_SIZE (16 * 1024)
#define STNS_MAXBUF 1024
#define STNS_MAX_NAME_LENGTH 32

#define STNS_HTTP_NOTFOUND 404L

#define STNS_LOCK_RETRY 3
#define STNS_LOCK_INTERVAL_MSEC 10

/* stns_request() return values (deliberately not CURLcode). */
#define STNS_OK 0
#define STNS_NG 1

/*
 * "The caller's buffer was too small", as returned by the lookup layer.
 *
 * This deliberately is not an NS_* constant.  On NetBSD, NS_RETURN is a source
 * action rather than a status, and its value collides with NS_SUCCESS, so
 * "buffer too small" is reported the way NetBSD's own backends report it:
 * *retval = ERANGE and NS_UNAVAIL.  The glue in nss_stns.c does that
 * translation.
 */
#define STNS_NS_ERANGE (-1)

typedef struct stns_response_t stns_response_t;
struct stns_response_t {
	char *data;
	size_t size;
	long status_code;
};

typedef struct stns_http_header_t stns_http_header_t;
struct stns_http_header_t {
	char *key;
	char *value;
};

typedef struct stns_conf_t stns_conf_t;
struct stns_conf_t {
	char *api_endpoint;
	char *auth_token;
	char *user;
	char *password;
	char *query_wrapper;
	char *chain_ssh_wrapper;
	char *http_proxy;
	char *cache_dir;
	char *tls_ca;
	char *tls_cert;
	char *tls_key;
	char *cached_unix_socket;

	stns_http_header_t *http_headers;
	size_t http_headers_size;

	int cached_enable;
	int uid_shift;
	int gid_shift;
	int cache;
	int cache_ttl;
	int negative_cache_ttl;
	int request_retry;
	int request_locktime;

	long request_timeout;
	long ssl_verify;
	long http_location;

	/*
	 * How many keys in the file we did not recognise.  An absent key is
	 * normal - nearly everything here is optional and has a default - but
	 * a key that is present and unrecognised is a typo or a setting only
	 * the Linux client implements, and either way it is doing nothing.
	 */
	int unknown_keys;
};

/* stns_config.c */
int stns_load_config(const char *filename, stns_conf_t *c);
void stns_unload_config(stns_conf_t *c);
const char *stns_config_path(void);

/* stns_request.c */
int stns_request(stns_conf_t *c, const char *path, stns_response_t *res);
int stns_exec_cmd(const char *cmd, const char *arg, stns_response_t *r);
char *stns_escape_path(const char *path);

/*
 * The API advertises the highest/lowest managed uid/gid through response
 * headers.  Remembering them lets us skip pointless HTTP round trips for ids
 * that STNS can never own (every local account, for instance).
 */
void stns_set_user_highest_id(int id);
void stns_set_user_lowest_id(int id);
void stns_set_group_highest_id(int id);
void stns_set_group_lowest_id(int id);
int stns_user_id_queryable(int id);
int stns_group_id_queryable(int id);

int stns_is_valid_name(const char *name);
int stns_mutex_retrylock(pthread_mutex_t *mutex);

/* stns_nss.c - OS independent lookup logic, returns NS_* */
int stns_pw_by_name(stns_conf_t *c, const char *name, struct passwd *pwd, char *buf, size_t buflen, int *errnop);
int stns_pw_by_uid(stns_conf_t *c, uid_t uid, struct passwd *pwd, char *buf, size_t buflen, int *errnop);
int stns_pw_setent(void);
int stns_pw_endent(void);
int stns_pw_nextent(stns_conf_t *c, struct passwd *pwd, char *buf, size_t buflen, int *errnop);

int stns_gr_by_name(stns_conf_t *c, const char *name, struct group *grp, char *buf, size_t buflen, int *errnop);
int stns_gr_by_gid(stns_conf_t *c, gid_t gid, struct group *grp, char *buf, size_t buflen, int *errnop);
int stns_gr_setent(void);
int stns_gr_endent(void);
int stns_gr_nextent(stns_conf_t *c, struct group *grp, char *buf, size_t buflen, int *errnop);
int stns_gr_membership(stns_conf_t *c, const char *uname, gid_t agroup, gid_t *groups, int maxgrp, int *groupc);

/* stns_entry.c */
int stns_fill_passwd(JSON_Object *o, stns_conf_t *c, struct passwd *pwd, char *buf, size_t buflen, int *errnop);
int stns_fill_group(JSON_Object *o, stns_conf_t *c, struct group *grp, char *buf, size_t buflen, int *errnop);

#endif /* STNS_H */
