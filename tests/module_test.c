/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 zakinko
 *
 * Load the built module the way libc does and check what it registers.
 *
 * This catches the class of bug that is invisible end to end: if the module
 * fails to load, or registers under the wrong method name, every lookup simply
 * falls through to the next source and the system looks like a correctly
 * configured host whose directory happens to be empty.  Here it is an error.
 */
#include <dlfcn.h>
#include <nsswitch.h>
#include <stdio.h>
#include <string.h>

#if defined(__NetBSD__)
#define EXPECT_NETBSD 1
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#define EXPECT_FREEBSD 1
#else
#error "unsupported platform"
#endif

struct expected {
	const char *database;
	const char *name;
};

static const struct expected expected[] = {
	{ NSDB_PASSWD, "getpwnam_r" },
	{ NSDB_PASSWD, "getpwuid_r" },
	{ NSDB_PASSWD, "getpwent_r" },
	{ NSDB_PASSWD, "setpwent" },
	{ NSDB_PASSWD, "endpwent" },
	{ NSDB_GROUP, "getgrnam_r" },
	{ NSDB_GROUP, "getgrgid_r" },
	{ NSDB_GROUP, "getgrent_r" },
	{ NSDB_GROUP, "setgrent" },
	{ NSDB_GROUP, "endgrent" },
	{ NSDB_GROUP, "getgroupmembership" },
#ifdef EXPECT_NETBSD
	/*
	 * NetBSD dispatches the non-reentrant entry points too, and has
	 * setpassent()/setgroupent(); missing any of them would leave
	 * getpwnam(3) resolving nothing while getpwnam_r(3) worked.
	 */
	{ NSDB_PASSWD, "getpwnam" },
	{ NSDB_PASSWD, "getpwuid" },
	{ NSDB_PASSWD, "getpwent" },
	{ NSDB_PASSWD, "setpassent" },
	{ NSDB_GROUP, "getgrnam" },
	{ NSDB_GROUP, "getgrgid" },
	{ NSDB_GROUP, "getgrent" },
	{ NSDB_GROUP, "setgroupent" },
#endif
};

static int checks;
static int failures;

static void
check(int cond, const char *what)
{
	checks++;
	if (cond) {
		(void)printf("ok   - %s\n", what);
	} else {
		failures++;
		(void)printf("FAIL - %s\n", what);
	}
}

int
main(int argc, char *argv[])
{
	nss_module_register_fn reg;
	nss_module_unregister_fn unreg = NULL;
	unsigned int mtabsize = 0;
	ns_mtab *mtab;
	void *handle;
	size_t i;
	unsigned int j;

	if (argc != 2) {
		(void)fprintf(stderr, "usage: module_test <module.so>\n");
		return 1;
	}

	/* RTLD_NOW so that an unresolved symbol fails here and not in sshd. */
	if ((handle = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW)) == NULL) {
		(void)fprintf(stderr, "FAIL - dlopen(%s): %s\n", argv[1], dlerror());
		return 1;
	}
	check(1, "the module loads with RTLD_NOW");

	reg = (nss_module_register_fn)dlsym(handle, "nss_module_register");
	if (reg == NULL) {
		(void)fprintf(stderr, "FAIL - dlsym(nss_module_register): %s\n", dlerror());
		return 1;
	}
	check(1, "nss_module_register resolves");

	mtab = reg("stns", &mtabsize, &unreg);
	check(mtab != NULL, "nss_module_register returns a method table");
	check(mtabsize > 0, "the method table is not empty");
	if (mtab == NULL || mtabsize == 0)
		return 1;

	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		char what[128];
		int found = 0;

		for (j = 0; j < mtabsize; j++) {
			if (mtab[j].database == NULL || mtab[j].name == NULL)
				continue;
			if (strcmp(mtab[j].database, expected[i].database) == 0 &&
			    strcmp(mtab[j].name, expected[i].name) == 0) {
				found = mtab[j].method != NULL;
				break;
			}
		}
		(void)snprintf(what, sizeof(what), "%s/%s is registered", expected[i].database, expected[i].name);
		check(found, what);
	}

	/* Anything registered that libc will never call is dead weight. */
	for (j = 0; j < mtabsize; j++) {
		char what[128];
		int known = 0;

		for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
			if (mtab[j].database != NULL && mtab[j].name != NULL &&
			    strcmp(mtab[j].database, expected[i].database) == 0 &&
			    strcmp(mtab[j].name, expected[i].name) == 0) {
				known = 1;
				break;
			}
		}
		(void)snprintf(what, sizeof(what), "%s/%s is expected", mtab[j].database ? mtab[j].database : "(null)",
		    mtab[j].name ? mtab[j].name : "(null)");
		check(known, what);
	}

	(void)printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
