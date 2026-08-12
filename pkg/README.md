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
| ports (FreeBSD) | [`ports/net/nss_stns`](ports/net/nss_stns) | `net`, beside `nss-pam-ldapd` and `nss_ldap` |

The categories differ because the collections themselves differ: pkgsrc keeps
directory clients under `security`, the ports tree under `net`.

## pkgsrc

pkgsrc is not only NetBSD's — it bootstraps on FreeBSD too, and there the
module is `nss_stns.so.1`. The package works that out from `OPSYS`, so one
package directory serves both; `ONLY_FOR_PLATFORM` keeps it from being tried
where there is no nsswitch module interface at all.

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

## ports

```sh
cp -R pkg/ports/net/nss_stns /usr/ports/net/
cd /usr/ports/net/nss_stns

make makesum          # fetch the release tarball and write distinfo
make install clean
```

Before sending it anywhere, stage it and let the framework check the packing
list against what was really installed:

```sh
make stage
make check-plist
make package
```

And to undo it:

```sh
make deinstall
```

## Building from a checkout instead of a release

Every package fetches a release tarball from GitHub. To build the working tree
instead, hand the framework a tarball made from it, named the way the
framework expects:

```sh
V=0.1.0
mkdir -p /tmp/dist/nss_stns-$V
tar --exclude .git -cf - . | (cd /tmp/dist/nss_stns-$V && tar -xf -)

# pkgsrc
tar -C /tmp/dist -czf /usr/pkgsrc/distfiles/nss_stns-$V.tar.gz nss_stns-$V

# ports
tar -C /tmp/dist -czf \
    /usr/ports/distfiles/zakinko-nss_stns-v${V}_GH0.tar.gz nss_stns-$V
```

Then `make makesum` in the package directory picks up the tarball that is
already there rather than fetching one, and the build proceeds normally.
This is what the Packaging workflow does.

Note that a tarball made this way carries the `$SNOWRABBIT$` ident line in
`stns.conf.example` unexpanded, because `tar` is not `git archive`. Use
`git archive --prefix=nss_stns-$V/ -o <file> HEAD` if that matters.

## The one thing every package has to do by hand

`make install` normally symlinks the module into `/usr/lib`, because libc
calls `dlopen("nss_stns.so.<version>")` with a bare name and the run-time
linker restricts a set-user-ID program to `/lib` and `/usr/lib`. Without the
symlink, `su(1)` and `login(1)` silently fail to resolve STNS accounts.

That path is outside `PREFIX`, which no package manager will stage. Every
package therefore builds with `NSSLIBDIR=${PREFIX}/lib` to suppress the
symlink and creates it from its own hooks instead:

- pkgsrc: the `INSTALL` and `DEINSTALL` scripts
- ports: `@postexec` and `@postunexec` in `pkg-plist`
