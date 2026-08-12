PORTNAME=	nss_stns
DISTVERSIONPREFIX=	v
DISTVERSION=	0.1.0
CATEGORIES=	net

MAINTAINER=	zakinko@snowrabbit.org
COMMENT=	STNS name service switch module
WWW=		https://github.com/zakinko/nss_stns

LICENSE=	BSD2CLAUSE
LICENSE_FILE=	${WRKSRC}/LICENSE

LIB_DEPENDS=	libcurl.so:ftp/curl

USES=		localbase
USE_GITHUB=	yes
GH_ACCOUNT=	zakinko

# The upstream Makefile is BSD make, so it is driven directly.  NSSLIBDIR is
# pinned inside ${PREFIX} so nothing is staged outside it; the /usr/lib symlink
# libc actually needs is made from pkg-plist with @postexec instead.
MAKE_ARGS=	PREFIX=${PREFIX} \
		LOCALBASE=${LOCALBASE} \
		SYSCONFDIR=${PREFIX}/etc \
		NSSLIBDIR=${PREFIX}/lib

TEST_TARGET=	test

PLIST_SUB=	NSS_VERSION=1

.include <bsd.port.mk>
