# SPDX-License-Identifier: BSD-2-Clause
#
# nss_stns - STNS name service switch module for NetBSD, FreeBSD and DragonFly BSD.
# Written for BSD make; run it as "make".

OS!=		uname -s

.if ${OS} == "NetBSD"
# libc dlopen()s nss_<source>.so.<NSS_MODULE_INTERFACE_VERSION>, and that
# version number is 0 on NetBSD and 1 on FreeBSD and DragonFly.
NSS_VERSION=	0
LOCALBASE?=	/usr/pkg
.elif ${OS} == "FreeBSD" || ${OS} == "DragonFly" || ${OS} == "MidnightBSD"
NSS_VERSION=	1
LOCALBASE?=	/usr/local
.else
.error nss_stns supports NetBSD, FreeBSD and DragonFly BSD only, not ${OS}
.endif

PREFIX?=	${LOCALBASE}
# pkgsrc calls this PKG_SYSCONFDIR and FreeBSD ports ETCDIR; both default to
# ${PREFIX}/etc.  Override it if your pkgsrc is set up with PKG_SYSCONFDIR=/etc.
SYSCONFDIR?=	${PREFIX}/etc
LIBDIR?=	${PREFIX}/lib
BINDIR?=	${PREFIX}/bin
EXAMPLESDIR?=	${PREFIX}/share/examples/nss_stns
# Programs that run setuid only search the trusted directories, so the module
# has to be reachable from /usr/lib as well.
NSSLIBDIR?=	/usr/lib

MODULE=		nss_stns.so.${NSS_VERSION}
KEY_WRAPPER=	stns-key-wrapper
TEST=		stns_test
MODULE_TEST=	module_test

CC?=		cc
INSTALL?=	install
CFLAGS?=	-O2 -pipe
WARNS=		-Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes \
		-Wpointer-arith -Wno-unused-parameter
CPPFLAGS+=	-I${.CURDIR}/src -I${.CURDIR}/external/mit/parson -I${.CURDIR}/external/mit/tomlc99 -I${LOCALBASE}/include \
		-DSTNS_CONFDIR=\"${SYSCONFDIR}\"
# Hide everything but nss_module_register: the module is dlopen()ed into every
# process on the system and must not export symbols that collide with theirs.
PICFLAGS=	-fPIC -fvisibility=hidden
LDFLAGS+=	-L${LOCALBASE}/lib -Wl,-rpath,${LOCALBASE}/lib
LIBS+=		-lcurl

CORE_OBJS=	src/stns_config.o \
		src/stns_request.o \
		src/stns_entry.o \
		src/stns_nss.o \
		external/mit/parson/parson.o \
		external/mit/tomlc99/toml.o

MODULE_OBJS=	${CORE_OBJS} src/nss_stns.o
WRAPPER_OBJS=	${CORE_OBJS} src/stns_key_wrapper.o
TEST_OBJS=	${CORE_OBJS} tests/stns_test.o

OBJS=		${MODULE_OBJS} src/stns_key_wrapper.o tests/stns_test.o \
		tests/module_test.o

all: ${MODULE} ${KEY_WRAPPER}

.SUFFIXES: .c .o

.c.o:
	${CC} ${CFLAGS} ${WARNS} ${CPPFLAGS} ${PICFLAGS} -c ${.IMPSRC} -o ${.TARGET}

${MODULE}: ${MODULE_OBJS}
	${CC} -shared -Wl,-soname,${MODULE} -o ${.TARGET} ${MODULE_OBJS} ${LDFLAGS} ${LIBS}

${KEY_WRAPPER}: ${WRAPPER_OBJS}
	${CC} -o ${.TARGET} ${WRAPPER_OBJS} ${LDFLAGS} ${LIBS}

${TEST}: ${TEST_OBJS}
	${CC} -o ${.TARGET} ${TEST_OBJS} ${LDFLAGS} ${LIBS}

${MODULE_TEST}: tests/module_test.o
	${CC} -o ${.TARGET} tests/module_test.o

# Unit tests, then the module loaded exactly the way libc loads it.
test: ${TEST} ${MODULE_TEST} ${MODULE}
	./${TEST}
	./${MODULE_TEST} ./${MODULE}

symbols: ${MODULE}
	sh ${.CURDIR}/tests/check_symbols.sh ./${MODULE}

# Check that the sample configuration's ident line survives a release build.
ident:
	sh ${.CURDIR}/tests/check_ident.sh

# Check the bundled third party code against external/MANIFEST.  Add
# --upstream, as the External workflow does, to also ask github whether the
# recorded revisions are still current.
external:
	sh ${.CURDIR}/tests/check_external.sh

# Stage an install and diff it against the packaging lists under pkg/.
plist: all
	sh ${.CURDIR}/tests/check_plist.sh

# The same unit tests under AddressSanitizer, which is where the buffer
# marshalling bugs show up.
asan:
	${CC} -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
		${WARNS} ${CPPFLAGS} \
		src/stns_config.c src/stns_request.c src/stns_entry.c \
		src/stns_nss.c external/mit/parson/parson.c external/mit/tomlc99/toml.c tests/stns_test.c \
		${LDFLAGS} ${LIBS} -o ${TEST}-asan
	./${TEST}-asan

integration: all
	sh ${.CURDIR}/tests/integration.sh

install: install-module install-wrapper install-conf

install-module: ${MODULE}
	${INSTALL} -d ${DESTDIR}${LIBDIR}
	${INSTALL} -m 555 ${MODULE} ${DESTDIR}${LIBDIR}/${MODULE}
.if ${NSSLIBDIR} != ${LIBDIR}
	${INSTALL} -d ${DESTDIR}${NSSLIBDIR}
	ln -sf ${LIBDIR}/${MODULE} ${DESTDIR}${NSSLIBDIR}/${MODULE}
.endif

install-wrapper: ${KEY_WRAPPER}
	${INSTALL} -d ${DESTDIR}${BINDIR}
	${INSTALL} -m 555 ${KEY_WRAPPER} ${DESTDIR}${BINDIR}/${KEY_WRAPPER}

# The sample lives under share/examples the way pkgsrc and ports expect; the
# real file is left for the administrator to create.
install-conf:
	${INSTALL} -d ${DESTDIR}${EXAMPLESDIR}
	${INSTALL} -m 644 ${.CURDIR}/stns.conf.example ${DESTDIR}${EXAMPLESDIR}/stns.conf
	${INSTALL} -d ${DESTDIR}${SYSCONFDIR}/stns/client

deinstall:
	rm -f ${DESTDIR}${LIBDIR}/${MODULE}
	rm -f ${DESTDIR}${NSSLIBDIR}/${MODULE}
	rm -f ${DESTDIR}${BINDIR}/${KEY_WRAPPER}
	rm -f ${DESTDIR}${EXAMPLESDIR}/stns.conf

clean:
	rm -f ${OBJS} ${MODULE} ${KEY_WRAPPER} ${TEST} ${TEST}-asan ${MODULE_TEST}

.PHONY: all test symbols plist ident external asan integration install install-module \
	install-wrapper install-conf deinstall clean
