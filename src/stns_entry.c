/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 zakinko
 *
 * Marshalling of STNS JSON objects into struct passwd / struct group inside
 * the caller supplied buffer.
 */
#include <errno.h>
#include <stddef.h>

#include "stns.h"

/*
 * A bump allocator over the buffer that nsdispatch(3) handed us.  Running out
 * of room is not an error we can paper over: the caller has to retry with a
 * bigger buffer, which is what STNS_NS_ERANGE asks the OS glue to report.
 */
struct stns_buf {
	char *p;
	size_t left;
};

static void
buf_init(struct stns_buf *b, char *buf, size_t buflen)
{
	b->p = buf;
	b->left = buflen;
}

static int
buf_str(struct stns_buf *b, const char *s, char **out)
{
	size_t len;

	if (s == NULL)
		s = "";
	len = strlen(s) + 1;
	if (b->left < len)
		return STNS_NG;
	memcpy(b->p, s, len);
	*out = b->p;
	b->p += len;
	b->left -= len;
	return STNS_OK;
}

/* Carve out a NULL terminated char * array, honouring pointer alignment. */
static int
buf_ptrs(struct stns_buf *b, size_t n, char ***out)
{
	size_t pad, need;

	pad = (size_t)((uintptr_t)b->p % sizeof(char *));
	if (pad != 0)
		pad = sizeof(char *) - pad;
	need = pad + (n + 1) * sizeof(char *);
	if (b->left < need)
		return STNS_NG;

	b->p += pad;
	b->left -= pad;
	*out = (char **)(void *)b->p;
	b->p += (n + 1) * sizeof(char *);
	b->left -= (n + 1) * sizeof(char *);
	return STNS_OK;
}

/* Fetch a string field, or NULL if it is absent or of another type. */
static const char *
json_str(JSON_Object *o, const char *key)
{
	return json_value_get_string(json_object_get_value(o, key));
}

/*
 * Fetch a numeric field, or -1 if it is absent or of another type.  Ids are
 * never negative, so -1 is unambiguous.
 */
static int
json_int(JSON_Object *o, const char *key)
{
	JSON_Value *v = json_object_get_value(o, key);

	if (json_value_get_type(v) != JSONNumber)
		return -1;
	return (int)json_value_get_number(v);
}

/*
 * Convert one user record into a struct passwd whose strings all live in buf.
 *
 * A record missing a name or an id is reported as not found rather than as an
 * error: the rest of the listing is still perfectly good.
 */
int
stns_fill_passwd(JSON_Object *o, stns_conf_t *c, struct passwd *pwd, char *buf, size_t buflen, int *errnop)
{
	struct stns_buf b;
	char home[STNS_MAXBUF];
	const char *name, *shell, *dir, *gecos, *password;
	int id, group_id;

	name = json_str(o, "name");
	id = json_int(o, "id");
	group_id = json_int(o, "group_id");
	if (name == NULL || id < 0 || group_id < 0)
		return NS_NOTFOUND;

	gecos = json_str(o, "gecos");
	shell = json_str(o, "shell");
	dir = json_str(o, "directory");
	password = json_str(o, "password");

	if (shell == NULL || *shell == '\0')
		shell = STNS_DEFAULT_SHELL;
	if (dir == NULL || *dir == '\0') {
		(void)snprintf(home, sizeof(home), "%s/%s", STNS_DEFAULT_HOME_PREFIX, name);
		dir = home;
	}
	/*
	 * Mirror what the files backend does: the real hash is only handed to
	 * root, everybody else sees a locked password.  That is what lets
	 * pam_unix(8) authenticate without a separate shadow database.
	 */
	if (password == NULL || *password == '\0' || geteuid() != 0)
		password = "*";

	buf_init(&b, buf, buflen);
	pwd->pw_uid = (uid_t)(c->uid_shift + id);
	pwd->pw_gid = (gid_t)(c->gid_shift + group_id);
	pwd->pw_change = 0;
	pwd->pw_expire = 0;

	if (buf_str(&b, name, &pwd->pw_name) != STNS_OK || buf_str(&b, password, &pwd->pw_passwd) != STNS_OK ||
	    buf_str(&b, "", &pwd->pw_class) != STNS_OK || buf_str(&b, gecos, &pwd->pw_gecos) != STNS_OK ||
	    buf_str(&b, dir, &pwd->pw_dir) != STNS_OK || buf_str(&b, shell, &pwd->pw_shell) != STNS_OK) {
		*errnop = ERANGE;
		return STNS_NS_ERANGE;
	}

#ifdef STNS_NSS_FREEBSD
	pwd->pw_fields = _PWF_NAME | _PWF_PASSWD | _PWF_UID | _PWF_GID | _PWF_CLASS | _PWF_GECOS | _PWF_DIR | _PWF_SHELL;
#endif
	return NS_SUCCESS;
}

/*
 * Convert one group record into a struct group whose gr_mem array, and every
 * name in it, live in buf.
 */
int
stns_fill_group(JSON_Object *o, stns_conf_t *c, struct group *grp, char *buf, size_t buflen, int *errnop)
{
	struct stns_buf b;
	JSON_Array *members;
	const char *name;
	size_t i, n, kept;
	int id;

	name = json_str(o, "name");
	id = json_int(o, "id");
	if (name == NULL || id < 0)
		return NS_NOTFOUND;

	members = json_object_get_array(o, "users");
	n = (members != NULL) ? json_array_get_count(members) : 0;

	buf_init(&b, buf, buflen);
	grp->gr_gid = (gid_t)(c->gid_shift + id);

	if (buf_str(&b, name, &grp->gr_name) != STNS_OK || buf_str(&b, "*", &grp->gr_passwd) != STNS_OK ||
	    buf_ptrs(&b, n, &grp->gr_mem) != STNS_OK) {
		*errnop = ERANGE;
		return STNS_NS_ERANGE;
	}

	for (i = 0, kept = 0; i < n; i++) {
		const char *member = json_array_get_string(members, i);

		if (member == NULL)
			continue;
		if (buf_str(&b, member, &grp->gr_mem[kept]) != STNS_OK) {
			*errnop = ERANGE;
			return STNS_NS_ERANGE;
		}
		kept++;
	}
	grp->gr_mem[kept] = NULL;

	return NS_SUCCESS;
}
