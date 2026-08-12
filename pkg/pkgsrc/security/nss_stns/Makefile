# $NetBSD$

DISTNAME=	nss_stns-0.1.0
CATEGORIES=	security
MASTER_SITES=	${MASTER_SITE_GITHUB:=zakinko/}
GITHUB_PROJECT=	nss_stns
GITHUB_TAG=	v${PKGVERSION_NOREV}

MAINTAINER=	zakinko@snowrabbit.org
HOMEPAGE=	https://github.com/zakinko/nss_stns/
COMMENT=	STNS name service switch module
LICENSE=	2-clause-bsd

USE_LANGUAGES=	c
USE_TOOLS+=	pkg-config

# pkgsrc bootstraps far beyond NetBSD, and most of where it reaches has no
# nsswitch module interface at all - Linux has NSS but not this one, and
# macOS has Open Directory.  Say so here, so pkgsrc declines cleanly instead
# of letting the build fail halfway through with a compiler error.
ONLY_FOR_PLATFORM=	NetBSD-*-* FreeBSD-*-* MidnightBSD-*-*

.include "../../mk/bsd.prefs.mk"

# libc dlopen()s nss_<source>.so.<NSS_MODULE_INTERFACE_VERSION>.  That number
# is 0 on NetBSD and 1 on FreeBSD, and pkgsrc runs on both,
# so the file name cannot be written down once and left alone: a bootstrapped
# pkgsrc on FreeBSD builds nss_stns.so.1, and a PLIST naming
# .so.0 would fail the packing list check every time.  ONLY_FOR_PLATFORM above
# is what makes the else branch here mean exactly "FreeBSD".
.if ${OPSYS} == "NetBSD"
NSS_VERSION=	0
.else
NSS_VERSION=	1
.endif
PLIST_SUBST+=	NSS_VERSION=${NSS_VERSION}
FILES_SUBST+=	NSS_VERSION=${NSS_VERSION}

# The upstream Makefile is BSD make, so pkgsrc's bmake drives it directly.
# NSSLIBDIR is pinned inside ${PREFIX} so that nothing lands outside it during
# staging; the /usr/lib symlink libc actually needs is made by the INSTALL
# script instead, and removed again by DEINSTALL.
MAKE_FLAGS+=	PREFIX=${PREFIX}
MAKE_FLAGS+=	LOCALBASE=${PREFIX}
MAKE_FLAGS+=	SYSCONFDIR=${PKG_SYSCONFBASE}
MAKE_FLAGS+=	NSSLIBDIR=${PREFIX}/lib
MAKE_FLAGS+=	NSS_VERSION=${NSS_VERSION}
MAKE_FLAGS+=	CC=${CC:Q}

BUILD_DEFS+=	PKG_SYSCONFBASE

# The client configuration lives beside a server's, as it does upstream.
PKG_SYSCONFSUBDIR=	stns/client

EGDIR=		${PREFIX}/share/examples/nss_stns
CONF_FILES=	${EGDIR}/stns.conf ${PKG_SYSCONFDIR}/stns.conf

TEST_TARGET=	test

.include "../../www/curl/buildlink3.mk"
.include "../../mk/bsd.pkg.mk"
