# Packaging

[日本語](README.ja.md)

The upstream `Makefile` is BSD make and takes `PREFIX`, `SYSCONFDIR`,
`LIBDIR`, `BINDIR`, `EXAMPLESDIR`, `NSSLIBDIR` and `DESTDIR`, so each
collection's package is thin.

Below `pkg/` each package sits at the same path it does in its own
collection, so putting it in place is a plain `cp -R`.

| Collection | Directory | Category |
| --- | --- | --- |
| pkgsrc (NetBSD) | [`pkgsrc/security/nss_stns`](pkgsrc/security/nss_stns) | `security`, beside `nss-pam-ldapd` |

pkgsrc keeps its directory clients under `security`.

## pkgsrc

`ONLY_FOR_PLATFORM` keeps the package from being tried where there is no
nsswitch module interface at all, which is most of where pkgsrc bootstraps.

```sh
cp -R pkg/pkgsrc/security/nss_stns /usr/pkgsrc/security/
cd /usr/pkgsrc/security/nss_stns

make makesum          # fetch the release tarball and write distinfo
make install
```

Building every dependency from source says nothing about this package and
takes hours doing it. To pull them in as binary packages instead:

```sh
make DEPENDS_TARGET=bin-install install
```

Before sending it anywhere, run pkgsrc's own reviewer:

```sh
pkg_add pkglint
pkglint .
```

And to undo it:

```sh
make deinstall
```

## Building from a checkout instead of a release

The package fetches a release tarball from GitHub. To build the working tree
instead, hand the framework a tarball made from it, named the way the
framework expects:

```sh
V=0.1.0
mkdir -p /tmp/dist/nss_stns-$V
tar --exclude .git -cf - . | (cd /tmp/dist/nss_stns-$V && tar -xf -)

# pkgsrc
tar -C /tmp/dist -czf /usr/pkgsrc/distfiles/nss_stns-$V.tar.gz nss_stns-$V
```

Then `make makesum` in the package directory picks up the tarball that is
already there rather than fetching one, and the build proceeds normally.
This is what the Packaging workflow does.

Note that a tarball made this way carries the `$SNOWRABBIT$` ident line in
`stns.conf.example` unexpanded, because `tar` is not `git archive`. Use
`git archive --prefix=nss_stns-$V/ -o <file> HEAD` if that matters.

## The one thing the package has to do by hand

`make install` normally symlinks the module into `/usr/lib`, because libc
calls `dlopen("nss_stns.so.0")` with a bare name and the run-time
linker restricts a set-user-ID program to `/lib` and `/usr/lib`. Without the
symlink, `su(1)` and `login(1)` silently fail to resolve STNS accounts.

That path is outside `PREFIX`, which pkgsrc will not stage. The package
therefore builds with `NSSLIBDIR=${PREFIX}/lib` to suppress the symlink and
creates it from its `INSTALL` script instead, removing it again from
`DEINSTALL`.
