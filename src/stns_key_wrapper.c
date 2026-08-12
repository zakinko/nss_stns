/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 zakinko
 *
 * stns-key-wrapper - print a user's SSH public keys, for use as sshd_config's
 * AuthorizedKeysCommand.
 */
#include <errno.h>

#include "stns.h"

static void
usage(void)
{
	(void)fprintf(stderr, "usage: stns-key-wrapper user\n");
	exit(1);
}

/*
 * Some sites keep a second source of keys (LDAP, a local file, ...).  When
 * chain_ssh_wrapper is configured its output is emitted as well, so the two
 * can be rolled out side by side.
 */
static void
chain(stns_conf_t *c, const char *user)
{
	stns_response_t r;

	if (c->chain_ssh_wrapper == NULL)
		return;
	memset(&r, 0, sizeof(r));
	if (stns_exec_cmd(c->chain_ssh_wrapper, user, &r) == STNS_OK && r.data != NULL)
		(void)fputs(r.data, stdout);
	free(r.data);
}

/*
 * sshd runs this once per authentication attempt, for local accounts as much
 * as directory ones, so a user we do not know is not an error: it is an empty
 * key list and a zero exit status.  Exiting non-zero would put a line in the
 * log for every local login on the machine.
 */
int
main(int argc, char *argv[])
{
	char path[STNS_MAXBUF];
	stns_response_t r;
	stns_conf_t c;
	JSON_Value *root;
	JSON_Array *arr;
	const char *user;
	size_t i, n;
	int found = 0;

	if (argc != 2)
		usage();
	user = argv[1];

	if (!stns_is_valid_name(user)) {
		(void)fprintf(stderr, "stns-key-wrapper: invalid user name\n");
		return 1;
	}

	if (stns_load_config(stns_config_path(), &c) != STNS_OK) {
		(void)fprintf(stderr, "stns-key-wrapper: cannot load %s\n", stns_config_path());
		return 1;
	}

	(void)snprintf(path, sizeof(path), "users?name=%s", user);
	if (stns_request(&c, path, &r) != STNS_OK) {
		free(r.data);
		chain(&c, user);
		stns_unload_config(&c);
		/*
		 * Exiting non-zero would make sshd log an error for every
		 * purely local account, so a miss is simply an empty key list.
		 */
		return 0;
	}

	root = json_parse_string(r.data);
	free(r.data);
	if (root == NULL) {
		(void)fprintf(stderr, "stns-key-wrapper: cannot parse the API response\n");
		stns_unload_config(&c);
		return 1;
	}

	if ((arr = json_value_get_array(root)) != NULL) {
		n = json_array_get_count(arr);
		for (i = 0; i < n && !found; i++) {
			JSON_Object *o = json_array_get_object(arr, i);
			JSON_Array *keys;
			const char *name;
			size_t j, m;

			if (o == NULL)
				continue;
			name = json_value_get_string(json_object_get_value(o, "name"));
			if (name == NULL || strcmp(name, user) != 0)
				continue;

			found = 1;
			if ((keys = json_object_get_array(o, "keys")) == NULL)
				continue;
			m = json_array_get_count(keys);
			for (j = 0; j < m; j++) {
				const char *key = json_array_get_string(keys, j);

				if (key != NULL)
					(void)printf("%s\n", key);
			}
		}
	}

	json_value_free(root);
	chain(&c, user);
	stns_unload_config(&c);
	return 0;
}
