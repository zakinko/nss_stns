# nss_stns

[日本語](README.ja.md)

An [STNS](https://stns.jp) name service switch module for **NetBSD**, **FreeBSD**
and **DragonFly BSD**.

STNS's own client, [STNS/libnss](https://github.com/STNS/libnss), targets glibc's
NSS. This is a separate implementation against the BSD `nsswitch(5)` module
interface, so that `getpwnam(3)`, `getgrnam(3)`, `getgrouplist(3)` and friends
resolve users and groups from an STNS API server.

NetBSD is the reference platform — it designed this interface, and it has the
larger surface, dispatching the non-reentrant entry points as well as the `_r`
ones. FreeBSD and DragonFly are adaptations of what works there.

The client configuration file is the same `stns.conf` you already use on Linux.

## Status

| | NetBSD | FreeBSD | DragonFly |
| --- | --- | --- | --- |
| passwd lookups (name / uid / enumeration) | yes | yes | yes |
| group lookups (name / gid / enumeration) | yes | yes | yes |
| supplementary groups (`getgroupmembership`) | yes | yes | yes |
| non-reentrant `getpwnam(3)` / `getgrnam(3)` | yes | n/a | n/a |
| `stns-key-wrapper` for `AuthorizedKeysCommand` | yes | yes | yes |
| `cache-stnsd` unix socket | yes | yes | yes |
| CI | yes | yes | yes |

FreeBSD derivatives such as MidnightBSD, GhostBSD and HardenedBSD define
`__FreeBSD__` and build from the same branch. DragonFly's DPorts is generated
from the FreeBSD Ports Collection rather than submitted to, so there is one
port entry rather than two; see [`pkg/`](pkg/).

OpenBSD and macOS are out of scope: neither has an nsswitch module interface at
all. OpenBSD's only pluggable directory source is YP, which is why its base
system ships `ypldap(8)`; macOS uses Open Directory. Supporting either means a
daemon, not a port of this module.

## Building and installing

Requires libcurl and BSD make.

```sh
# NetBSD
pkgin install curl
make
make install          # PREFIX defaults to /usr/pkg

# FreeBSD / DragonFly
pkg install curl
make
make install          # PREFIX defaults to /usr/local
```

`make install` places:

| File | NetBSD | FreeBSD / DragonFly |
| --- | --- | --- |
| module | `/usr/pkg/lib/nss_stns.so.0` | `/usr/local/lib/nss_stns.so.1` |
| module symlink | `/usr/lib/nss_stns.so.0` | `/usr/lib/nss_stns.so.1` |
| key wrapper | `/usr/pkg/bin/stns-key-wrapper` | `/usr/local/bin/stns-key-wrapper` |
| sample config | `/usr/pkg/share/examples/nss_stns/stns.conf` | `/usr/local/share/examples/nss_stns/stns.conf` |

Two details about that layout are worth knowing.

**Why the `/usr/lib` symlink.** libc loads the module with
`dlopen("nss_stns.so.<version>")` — a bare name, no path. For a set-user-ID
program the runtime linker only searches the trusted directories, `/lib` and
`/usr/lib`, so a module living only under `LOCALBASE` would silently fail to
load for `su(1)`, `login(1)` and anything else that runs privileged. The
symlink is what makes those work.

**Why the version suffix differs.** The file name ends in
`NSS_MODULE_INTERFACE_VERSION`, which is `0` on NetBSD and `1` on FreeBSD and
DragonFly. The `Makefile` picks the right one from `uname -s`.

Override the paths as usual:

```sh
make PREFIX=/opt/stns SYSCONFDIR=/etc install
```

Packages for pkgsrc, the ports tree and DPorts are in [`pkg/`](pkg/), with the
commands for dropping them into `/usr/pkgsrc`, `/usr/ports` and `/usr/dports`
and building them there.

## Configuration

### nsswitch.conf

This one is not ours to place: it belongs to the base system and its location
is fixed as `_PATH_NS_CONF` in `<nsswitch.h>`, so it stays in `/etc` on every
one of these systems regardless of where the package itself was installed. Add
`stns` after `files`:

```text
passwd: files stns
group:  files stns
```

Keeping `files` first means the local accounts in `/etc/passwd` always win and
the machine stays usable when the API server is unreachable.

> On NetBSD the shipped default is `passwd: compat`. Either change it to
> `files stns` as above, or leave `compat` in place and add `stns` to
> `passwd_compat`/`group_compat`.

### stns.conf

The config file is read from the first of these that exists:

1. `${SYSCONFDIR}/stns/client/stns.conf` — `/usr/pkg/etc/...` on NetBSD (pkgsrc's
   `PKG_SYSCONFDIR`), `/usr/local/etc/...` on FreeBSD
2. `/etc/stns/client/stns.conf`

Both keep upstream STNS's `stns/client/` subtree rather than flattening to a
bare `stns.conf`. A single-file module like `nss-pam-ldapd` can get away with a
flat `nslcd.conf`, but STNS is a suite: the server's configuration is
`stns/server/stns.conf`, so a flat name here would turn ambiguous the day an
STNS server is packaged for these systems too. `sssd` splits its configuration
the same way and for the same reason.

The second path is where Linux hosts keep it, and it is honoured deliberately:
a `stns.conf` can be copied from a Linux machine and dropped in unchanged. The
key names, tables and defaults all match `STNS/libnss`.

A minimal one looks like this:

```toml
api_endpoint = "https://stns.example.com/v1"
auth_token   = "xxxxxxxxxxxxxxxx"

uid_shift = 10000
gid_shift = 10000

[tls]
ca = "/usr/pkg/etc/stns/client/ca.pem"
```

See [`stns.conf.example`](stns.conf.example) for every supported key.

### Passwords

There is no `/etc/shadow` on a BSD, so the module does what the `files` backend
does: the `password` hash from the API is returned in `pw_passwd` only when the
caller's effective uid is 0, and everyone else sees `*`. That is enough for
`pam_unix(8)` to authenticate STNS users, with no shadow database involved.

### SSH keys

```text
# /etc/ssh/sshd_config
AuthorizedKeysCommand /usr/pkg/bin/stns-key-wrapper
AuthorizedKeysCommandUser nobody
```

`chain_ssh_wrapper` in `stns.conf` names a second command whose output is
appended, which makes it possible to migrate from an existing
`AuthorizedKeysCommand` gradually.

### cache-stnsd

To go through [cache-stnsd](https://github.com/STNS/cache-stnsd) instead of
talking to the API directly:

```toml
[cached]
enable      = true
unix_socket = "/var/run/cache-stnsd.sock"
```

## How it behaves when things go wrong

A name service module runs inside every process on the machine, so failure
handling matters more than throughput.

- **Responses are cached on disk**, under `cache_dir/<euid>/`, keyed by request
  path. Each user gets a private directory and the module refuses to read or
  overwrite a file it does not own. A 404 is remembered too, as a zero length
  file with its own shorter `negative_cache_ttl`. `cache_dir` defaults to
  `/var/db/stns` on NetBSD and `/var/cache/stns` on FreeBSD and DragonFly,
  because `hier(7)` documents `/var/cache` on the latter and nothing of the
  sort on the former.
- **A connection failure trips a circuit breaker** for `request_locktime`
  seconds, so an unreachable server costs one timeout rather than one per
  lookup. The breaker file lives in the caller's own cache directory rather
  than somewhere world writable, so an unprivileged user cannot wedge name
  resolution for everybody else.
- **Id range hints from the API** (`User-Highest-Id` and friends) are used to
  skip requests for uids the server could not possibly own.
- **An unrecognised key in `stns.conf` is reported**, once per process, at
  `LOG_NOTICE`. An absent key is not: nearly everything is optional with a
  documented default, and saying so on every lookup would drown syslog in
  news about a working configuration. A key that is *present* and
  unrecognised is different — write `api_endpont` and the module would
  otherwise fall back to `localhost` and fail every lookup with nothing said
  anywhere. `[http_headers]` is exempt, being open ended by design.
- **Names are validated before use.** Anything outside
  `[A-Za-z_][A-Za-z0-9._-]{0,31}` is rejected without a request, which is what
  keeps a crafted name from injecting extra query parameters.
- **`curl` runs with `CURLOPT_NOSIGNAL`** and the module exports only
  `nss_module_register`, so it cannot disturb the host process's signal
  handling or symbol table.

## Testing

```sh
make test          # unit tests, plus the module loaded the way libc loads it
make symbols       # assert the module exports nss_module_register and nothing else
make plist         # diff a staged install against the packing lists
make ident         # assert the sample config's ident line survives a release
make external      # check the bundled third party code against its manifest
make asan          # the unit tests under AddressSanitizer and UBSan
make integration   # end to end, needs root and python3
```

**`make test`** covers config parsing (including a `stns.conf` written for the
Linux module verbatim), name validation, cache key escaping, id range hints and
the buffer marshalling — the last by walking every buffer size from zero upward
and requiring a clean `ERANGE` at each one. It then `dlopen`s the built module
with `RTLD_NOW`, calls `nss_module_register` and checks the method table entry
by entry. That last part matters: if the module fails to register, every lookup
silently falls through to the next source and the system looks like a correctly
configured host whose directory happens to be empty.

**`make symbols`** fails if `nss_module_register` is not exported, and equally
if anything else is — the module is loaded into every process on the machine.

**`make integration`** starts [`tests/mock_stns_server.py`](tests/mock_stns_server.py),
points `/etc/nsswitch.conf` at the module and drives it through `getent(1)`,
`id(1)` and `stns-key-wrapper`. It checks lookups by name and id, defaults for
empty fields, enumeration, supplementary group merging, a 300 member group
(which forces the retry-with-a-bigger-buffer path through libc), id shifts,
`auth_token` / basic auth / `http_headers` actually reaching the server, unsafe
names being refused without any request at all, on-disk and negative caching
with the server stopped, an unprivileged caller, `query_wrapper`, the
`cache-stnsd` unix socket, and that local accounts keep resolving when the API
is unreachable, the config is corrupt, or the config is missing.

It runs the whole thing over TLS as well, against a CA it generates, and each
case there is paired with the one that must fail: the right CA resolves and no
trust anchor does not, `ssl_verify = false` accepts what verification would
reject, and a client certificate is presented when the server demands one while
its absence is refused. A test that only proved "https works" would pass just
as well with verification switched off.

The endpoint is exercised over IPv6 as well - plain, over TLS, and with a
client certificate - and with the address written two ways — `[::1]` and `[0:0:0:0:0:0:0:1]` are one
address spelled twice, and the module hands whichever it was given straight to
curl. A system with no `::1` to bind says so in the log rather than passing
quietly.

Two records exist to be awkward. One user has ten keys, the last of them the
length of an RSA 4096 key with a long comment, so the wrapper has to return all
of them and each one whole. Another has a gecos and a home directory far larger
than any first-guess buffer, so libc itself walks the ERANGE-and-retry path
rather than only the unit tests.

It rewrites `/etc/nsswitch.conf` (restoring it afterwards), so run it in a VM
or in CI.

**`make external`** checks `external/` against
[`external/MANIFEST`](external/MANIFEST), which records where each bundled
component came from and at exactly which upstream revision. git has no
equivalent of the CVS import tag NetBSD would use for this, so it is written
down. A checksum mismatch means somebody edited a bundled copy in place, which
defeats the point of recording a revision at all.

CI runs all of it on NetBSD, FreeBSD and DragonFly, on every push and once a
week — the VM images and their package sets drift underneath us, and a
scheduled build is what notices. A separate weekly job additionally asks
GitHub whether the revisions in the manifest are still upstream's current
ones, and whether upstream at those revisions still matches what is bundled,
byte for byte. Without it a bundled parser can go years out of date without
anything saying so.

## Licence

BSD 2-Clause, matching NetBSD. Portions are derived from
[STNS/libnss](https://github.com/STNS/libnss), and `external/mit/` bundles
[parson](https://github.com/kgabis/parson) and
[tomlc99](https://github.com/cktan/tomlc99) — the directory is named for the
licence, the way NetBSD names its own `external/` tree. All three are MIT.
See [LICENSE](LICENSE).
