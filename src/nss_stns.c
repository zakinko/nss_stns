/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 zakinko
 *
 * nsswitch(5) glue.
 *
 * libc hands each method its arguments through a va_list whose shape depends
 * on the method, and expects an NS_* status back.  NetBSD passes the result
 * through the first variadic argument and leaves nsdispatch(3)'s own retval
 * unused, and it dispatches the non-reentrant getpwnam()/getgrnam()/... entry
 * points as well as the _r ones, so those need storage owned by the module.
 *
 * Everything below the argument unpacking is shared with the rest of the
 * module and knows nothing about any of this.
 */
#include <errno.h>

#include "stns.h"

#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum stns_how { STNS_BY_NAME, STNS_BY_ID, STNS_NEXT };

static int
pw_lookup(stns_conf_t *c, enum stns_how how, const char *name, uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
    int *errnop)
{
	switch (how) {
	case STNS_BY_NAME:
		return stns_pw_by_name(c, name, pwd, buf, buflen, errnop);
	case STNS_BY_ID:
		return stns_pw_by_uid(c, uid, pwd, buf, buflen, errnop);
	default:
		return stns_pw_nextent(c, pwd, buf, buflen, errnop);
	}
}

static int
gr_lookup(stns_conf_t *c, enum stns_how how, const char *name, gid_t gid, struct group *grp, char *buf, size_t buflen,
    int *errnop)
{
	switch (how) {
	case STNS_BY_NAME:
		return stns_gr_by_name(c, name, grp, buf, buflen, errnop);
	case STNS_BY_ID:
		return stns_gr_by_gid(c, gid, grp, buf, buflen, errnop);
	default:
		return stns_gr_nextent(c, grp, buf, buflen, errnop);
	}
}

/*
 * The configuration is re-read for every lookup.  It is a small TOML file and
 * doing so means an administrator's edit takes effect immediately, without
 * having to restart every long lived daemon on the box.
 */
static int
pw_dispatch(enum stns_how how, const char *name, uid_t uid, struct passwd *pwd, char *buf, size_t buflen, int *errnop)
{
	stns_conf_t c;
	int rv;

	if (stns_load_config(stns_config_path(), &c) != STNS_OK)
		return NS_UNAVAIL;
	rv = pw_lookup(&c, how, name, uid, pwd, buf, buflen, errnop);
	stns_unload_config(&c);
	return rv;
}

static int
gr_dispatch(enum stns_how how, const char *name, gid_t gid, struct group *grp, char *buf, size_t buflen, int *errnop)
{
	stns_conf_t c;
	int rv;

	if (stns_load_config(stns_config_path(), &c) != STNS_OK)
		return NS_UNAVAIL;
	rv = gr_lookup(&c, how, name, gid, grp, buf, buflen, errnop);
	stns_unload_config(&c);
	return rv;
}

/*
 * Storage for the non-reentrant entry points.  libc holds its own passwd and
 * group mutexes across the nsdispatch(3) call, so this needs no extra locking.
 */
#define STNS_STATIC_MIN 1024
#define STNS_STATIC_MAX (256 * 1024)

struct stns_static {
	char *buf;
	size_t buflen;
};

static struct stns_static pw_static;
static struct stns_static gr_static;
static struct passwd pw_result;
static struct group gr_result;

static int
static_grow(struct stns_static *s)
{
	size_t want;
	char *grown;

	if (s->buflen == 0)
		want = STNS_STATIC_MIN;
	else if (s->buflen >= STNS_STATIC_MAX)
		return STNS_NG;
	else
		want = s->buflen * 2;

	if ((grown = realloc(s->buf, want)) == NULL)
		return STNS_NG;
	s->buf = grown;
	s->buflen = want;
	return STNS_OK;
}

static int
pw_static_lookup(enum stns_how how, const char *name, uid_t uid, struct passwd **result)
{
	int rv, dummy;

	*result = NULL;
	for (;;) {
		if (static_grow(&pw_static) != STNS_OK)
			return NS_UNAVAIL;
		dummy = 0;
		rv = pw_dispatch(how, name, uid, &pw_result, pw_static.buf, pw_static.buflen, &dummy);
		if (rv != STNS_NS_ERANGE)
			break;
	}
	if (rv == NS_SUCCESS)
		*result = &pw_result;
	return rv;
}

static int
gr_static_lookup(enum stns_how how, const char *name, gid_t gid, struct group **result)
{
	int rv, dummy;

	*result = NULL;
	for (;;) {
		if (static_grow(&gr_static) != STNS_OK)
			return NS_UNAVAIL;
		dummy = 0;
		rv = gr_dispatch(how, name, gid, &gr_result, gr_static.buf, gr_static.buflen, &dummy);
		if (rv != STNS_NS_ERANGE)
			break;
	}
	if (rv == NS_SUCCESS)
		*result = &gr_result;
	return rv;
}

/*
 * NetBSD reports "buffer too small" the way its own backends do: NS_UNAVAIL
 * with ERANGE in *retval.  NS_RETURN is a source action here, not a status.
 */
static int
netbsd_status(int rv, int *retval)
{
	if (rv == STNS_NS_ERANGE) {
		*retval = ERANGE;
		return NS_UNAVAIL;
	}
	return rv;
}

/* ARGSUSED */
static int
stns_getpwnam(void *nsrv, void *nscb, va_list ap)
{
	struct passwd **retval = va_arg(ap, struct passwd **);
	const char *name = va_arg(ap, const char *);

	return pw_static_lookup(STNS_BY_NAME, name, 0, retval);
}

/* ARGSUSED */
static int
stns_getpwuid(void *nsrv, void *nscb, va_list ap)
{
	struct passwd **retval = va_arg(ap, struct passwd **);
	uid_t uid = va_arg(ap, uid_t);

	return pw_static_lookup(STNS_BY_ID, NULL, uid, retval);
}

/* ARGSUSED */
static int
stns_getpwent(void *nsrv, void *nscb, va_list ap)
{
	struct passwd **retval = va_arg(ap, struct passwd **);

	return pw_static_lookup(STNS_NEXT, NULL, 0, retval);
}

/* ARGSUSED */
static int
stns_getpwnam_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	const char *name = va_arg(ap, const char *);
	struct passwd *pwd = va_arg(ap, struct passwd *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct passwd **result = va_arg(ap, struct passwd **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(pw_dispatch(STNS_BY_NAME, name, 0, pwd, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = pwd;
	return rv;
}

/* ARGSUSED */
static int
stns_getpwuid_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	uid_t uid = va_arg(ap, uid_t);
	struct passwd *pwd = va_arg(ap, struct passwd *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct passwd **result = va_arg(ap, struct passwd **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(pw_dispatch(STNS_BY_ID, NULL, uid, pwd, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = pwd;
	return rv;
}

/* ARGSUSED */
static int
stns_getpwent_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	struct passwd *pwd = va_arg(ap, struct passwd *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct passwd **result = va_arg(ap, struct passwd **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(pw_dispatch(STNS_NEXT, NULL, 0, pwd, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = pwd;
	return rv;
}

/* ARGSUSED */
static int
stns_setpwent(void *nsrv, void *nscb, va_list ap)
{
	return stns_pw_setent();
}

/* ARGSUSED */
static int
stns_setpassent(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	int rv;

	rv = stns_pw_setent();
	*retval = (rv == NS_SUCCESS);
	return rv;
}

/* ARGSUSED */
static int
stns_endpwent(void *nsrv, void *nscb, va_list ap)
{
	return stns_pw_endent();
}

/* ARGSUSED */
static int
stns_getgrnam(void *nsrv, void *nscb, va_list ap)
{
	struct group **retval = va_arg(ap, struct group **);
	const char *name = va_arg(ap, const char *);

	return gr_static_lookup(STNS_BY_NAME, name, 0, retval);
}

/* ARGSUSED */
static int
stns_getgrgid(void *nsrv, void *nscb, va_list ap)
{
	struct group **retval = va_arg(ap, struct group **);
	gid_t gid = va_arg(ap, gid_t);

	return gr_static_lookup(STNS_BY_ID, NULL, gid, retval);
}

/* ARGSUSED */
static int
stns_getgrent(void *nsrv, void *nscb, va_list ap)
{
	struct group **retval = va_arg(ap, struct group **);

	return gr_static_lookup(STNS_NEXT, NULL, 0, retval);
}

/* ARGSUSED */
static int
stns_getgrnam_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	const char *name = va_arg(ap, const char *);
	struct group *grp = va_arg(ap, struct group *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct group **result = va_arg(ap, struct group **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(gr_dispatch(STNS_BY_NAME, name, 0, grp, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = grp;
	return rv;
}

/* ARGSUSED */
static int
stns_getgrgid_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	gid_t gid = va_arg(ap, gid_t);
	struct group *grp = va_arg(ap, struct group *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct group **result = va_arg(ap, struct group **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(gr_dispatch(STNS_BY_ID, NULL, gid, grp, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = grp;
	return rv;
}

/* ARGSUSED */
static int
stns_getgrent_r(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	struct group *grp = va_arg(ap, struct group *);
	char *buf = va_arg(ap, char *);
	size_t buflen = va_arg(ap, size_t);
	struct group **result = va_arg(ap, struct group **);
	int rv;

	*result = NULL;
	*retval = 0;
	rv = netbsd_status(gr_dispatch(STNS_NEXT, NULL, 0, grp, buf, buflen, retval), retval);
	if (rv == NS_SUCCESS)
		*result = grp;
	return rv;
}

/* ARGSUSED */
static int
stns_setgrent(void *nsrv, void *nscb, va_list ap)
{
	return stns_gr_setent();
}

/* ARGSUSED */
static int
stns_setgroupent(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	int rv;

	rv = stns_gr_setent();
	*retval = (rv == NS_SUCCESS);
	return rv;
}

/* ARGSUSED */
static int
stns_endgrent(void *nsrv, void *nscb, va_list ap)
{
	return stns_gr_endent();
}

/* ARGSUSED */
static int
stns_getgroupmembership(void *nsrv, void *nscb, va_list ap)
{
	int *retval = va_arg(ap, int *);
	const char *uname = va_arg(ap, const char *);
	gid_t agroup = va_arg(ap, gid_t);
	gid_t *groups = va_arg(ap, gid_t *);
	int maxgrp = va_arg(ap, int);
	int *groupc = va_arg(ap, int *);
	stns_conf_t c;
	int rv;

	*retval = 0;
	if (stns_load_config(stns_config_path(), &c) != STNS_OK)
		return NS_UNAVAIL;
	rv = stns_gr_membership(&c, uname, agroup, groups, maxgrp, groupc);
	stns_unload_config(&c);
	return rv;
}

static ns_mtab stns_methods[] = {
	{ NSDB_PASSWD, "getpwnam", stns_getpwnam, NULL },
	{ NSDB_PASSWD, "getpwnam_r", stns_getpwnam_r, NULL },
	{ NSDB_PASSWD, "getpwuid", stns_getpwuid, NULL },
	{ NSDB_PASSWD, "getpwuid_r", stns_getpwuid_r, NULL },
	{ NSDB_PASSWD, "getpwent", stns_getpwent, NULL },
	{ NSDB_PASSWD, "getpwent_r", stns_getpwent_r, NULL },
	{ NSDB_PASSWD, "setpwent", stns_setpwent, NULL },
	{ NSDB_PASSWD, "setpassent", stns_setpassent, NULL },
	{ NSDB_PASSWD, "endpwent", stns_endpwent, NULL },
	{ NSDB_GROUP, "getgrnam", stns_getgrnam, NULL },
	{ NSDB_GROUP, "getgrnam_r", stns_getgrnam_r, NULL },
	{ NSDB_GROUP, "getgrgid", stns_getgrgid, NULL },
	{ NSDB_GROUP, "getgrgid_r", stns_getgrgid_r, NULL },
	{ NSDB_GROUP, "getgrent", stns_getgrent, NULL },
	{ NSDB_GROUP, "getgrent_r", stns_getgrent_r, NULL },
	{ NSDB_GROUP, "setgrent", stns_setgrent, NULL },
	{ NSDB_GROUP, "setgroupent", stns_setgroupent, NULL },
	{ NSDB_GROUP, "endgrent", stns_endgrent, NULL },
	{ NSDB_GROUP, "getgroupmembership", stns_getgroupmembership, NULL },
};

/*
 * The one symbol libc looks up with dlsym(3).  Everything else is hidden by
 * -fvisibility=hidden so that a module loaded into every process on the system
 * cannot collide with the host program's symbols.
 *
 * The attribute has to lead the declaration: written after the return type it
 * would attach to the pointer type instead, which clang accepts silently but
 * gcc discards with "visibility attribute ignored on non-class types" - and a
 * hidden nss_module_register means the module never registers at all.
 */
__attribute__((visibility("default"))) ns_mtab *nss_module_register(const char *, unsigned int *,
    nss_module_unregister_fn *);

/* ARGSUSED */
ns_mtab *
nss_module_register(const char *source, unsigned int *mtabsize, nss_module_unregister_fn *unreg)
{
	*mtabsize = (unsigned int)__arraycount(stns_methods);
	*unreg = NULL;
	return stns_methods;
}
