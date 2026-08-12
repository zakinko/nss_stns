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

# pkgsrc bootstraps far beyond NetBSD, and this module is NetBSD's alone.
# Say so here, so pkgsrc declines cleanly instead of letting the build fail
# halfway through with a compiler error.
ONLY_FOR_PLATFORM=	NetBSD-*-*


# The upstream Makefile is BSD make, so pkgsrc's bmake drives it directly.
# NSSLIBDIR is pinned inside ${PREFIX} so that nothing lands outside it during
# staging; the /usr/lib symlink libc actually needs is made by the INSTALL
# script instead, and removed again by DEINSTALL.
MAKE_FLAGS+=	PREFIX=${PREFIX}
MAKE_FLAGS+=	LOCALBASE=${PREFIX}
MAKE_FLAGS+=	SYSCONFDIR=${PKG_SYSCONFBASE}
MAKE_FLAGS+=	NSSLIBDIR=${PREFIX}/lib
MAKE_FLAGS+=	CC=${CC:Q}

BUILD_DEFS+=	PKG_SYSCONFBASE

# The client configuration lives beside a server's, as it does upstream.
PKG_SYSCONFSUBDIR=	stns/client

EGDIR=		${PREFIX}/share/examples/nss_stns
CONF_FILES=	${EGDIR}/stns.conf ${PKG_SYSCONFDIR}/stns.conf

TEST_TARGET=	test

.include "../../www/curl/buildlink3.mk"
.include "../../mk/bsd.pkg.mk"
