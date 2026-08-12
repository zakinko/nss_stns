#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# End to end test: install the module, point nsswitch.conf at it and drive it
# through getent(1), id(1) and stns-key-wrapper against
# tests/mock_stns_server.py.
#
# Must be run as root, and it rewrites /etc/nsswitch.conf (restoring it on
# exit), so run it on a throwaway machine or in CI.

set -eu

PORT=${PORT:-11104}
PYTHON=${PYTHON:-python3}
SRCDIR=$(cd "$(dirname "$0")/.." && pwd)
MOCK="$SRCDIR/tests/mock_stns_server.py"
WORKDIR=${WORKDIR:-/tmp/nss_stns_it}

OS=$(uname -s)
case "$OS" in
NetBSD)		LOCALBASE=${LOCALBASE:-/usr/pkg}
		CACHEDIR=${CACHEDIR:-/var/db/stns} ;;
FreeBSD|MidnightBSD)
		LOCALBASE=${LOCALBASE:-/usr/local}
		CACHEDIR=${CACHEDIR:-/var/cache/stns} ;;
*)		echo "unsupported OS: $OS" >&2; exit 1 ;;
esac
SYSCONFDIR=${SYSCONFDIR:-$LOCALBASE/etc}
CONFDIR=$SYSCONFDIR/stns/client
CONF=$CONFDIR/stns.conf

failures=0
checks=0

ok() {
	checks=$((checks + 1))
	echo "ok   - $1"
}

fail() {
	checks=$((checks + 1))
	failures=$((failures + 1))
	echo "FAIL - $1"
}

# expect <description> <expected> <actual>
expect() {
	if [ "$2" = "$3" ]; then
		ok "$1"
	else
		fail "$1"
		echo "       expected: $2"
		echo "       actual:   $3"
	fi
}

# contains <description> <needle> <haystack>
contains() {
	case "$3" in
	*"$2"*)	ok "$1" ;;
	*)	fail "$1"; echo "       expected to contain: $2"; echo "       actual: $3" ;;
	esac
}

# lacks <description> <needle> <haystack>
lacks() {
	case "$3" in
	*"$2"*)	fail "$1"; echo "       expected NOT to contain: $2"; echo "       actual: $3" ;;
	*)	ok "$1" ;;
	esac
}

# succeeds <description> <command...>
succeeds() {
	desc=$1; shift
	if "$@" >/dev/null 2>&1; then ok "$desc"; else fail "$desc"; fi
}

# denies <description> <command...>
denies() {
	desc=$1; shift
	if "$@" >/dev/null 2>&1; then fail "$desc"; else ok "$desc"; fi
}

server_pid=""

stop_server() {
	if [ -n "$server_pid" ]; then
		kill "$server_pid" 2>/dev/null || true
		wait "$server_pid" 2>/dev/null || true
		server_pid=""
	fi
}

cleanup() {
	set +e
	stop_server
	[ -f /etc/nsswitch.conf.stns-bak ] && mv /etc/nsswitch.conf.stns-bak /etc/nsswitch.conf
	rm -rf "$CACHEDIR" "$WORKDIR"
	rm -f "$CONF"
}
trap cleanup EXIT INT TERM

# write_conf <body>  - the endpoint is prepended, the body is everything else.
write_conf() {
	mkdir -p "$CONFDIR"
	{
		echo "api_endpoint = \"http://127.0.0.1:$PORT/v1\""
		printf '%s\n' "$1"
	} > "$CONF"
	# A stale cache would mask the very change we just made.
	rm -rf "$CACHEDIR"
}

# write_raw_conf <body>  - no endpoint, for testing malformed files.
write_raw_conf() {
	mkdir -p "$CONFDIR"
	printf '%s\n' "$1" > "$CONF"
	rm -rf "$CACHEDIR"
}

wait_for_port() {
	i=0
	while [ $i -lt 60 ]; do
		if $PYTHON -c "
import socket, sys
s = socket.socket()
s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('127.0.0.1', $PORT)) == 0 else 1)
" 2>/dev/null; then
			return 0
		fi
		i=$((i + 1))
		sleep 0.2
	done
	echo "mock server did not come up" >&2
	exit 1
}

# start_server [extra args...]
start_server() {
	stop_server
	$PYTHON "$MOCK" "$PORT" "$@" &
	server_pid=$!
	wait_for_port
}

start_unix_server() {
	stop_server
	$PYTHON "$MOCK" --unix "$WORKDIR/cached.sock" &
	server_pid=$!
	i=0
	while [ $i -lt 60 ] && [ ! -S "$WORKDIR/cached.sock" ]; do
		i=$((i + 1))
		sleep 0.2
	done
	[ -S "$WORKDIR/cached.sock" ] || { echo "unix mock server did not come up" >&2; exit 1; }
}

mkdir -p "$WORKDIR"

echo "== installing the module =="
cd "$SRCDIR"
make install

echo "== pointing nsswitch.conf at stns =="
cp /etc/nsswitch.conf /etc/nsswitch.conf.stns-bak
sed -e 's/^passwd:.*/passwd: files stns/' \
    -e 's/^group:.*/group: files stns/' \
    /etc/nsswitch.conf.stns-bak > /etc/nsswitch.conf
grep -q '^passwd:' /etc/nsswitch.conf || echo 'passwd: files stns' >> /etc/nsswitch.conf
grep -q '^group:' /etc/nsswitch.conf || echo 'group: files stns' >> /etc/nsswitch.conf
cat /etc/nsswitch.conf

echo "== starting the mock API =="
start_server

echo
echo "== lookups by name and id =="
write_conf 'cache = false'

expect "getent passwd stnsuser" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"

expect "getent passwd 1001" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd 1001)"

# Empty shell/directory fall back to /bin/sh and /home/<name>.
expect "getent passwd stnsdefault (defaults applied)" \
	"stnsdefault:*:1002:1001::/home/stnsdefault:/bin/sh" \
	"$(getent passwd stnsdefault)"

expect "getent group stnsgroup" \
	"stnsgroup:*:1001:stnsuser,stnsdefault" \
	"$(getent group stnsgroup)"

expect "getent group 1002" \
	"stnsops:*:1002:stnsuser" \
	"$(getent group 1002)"

# NetBSD's getent(1) prints the trailing colon for an empty member list and
# FreeBSD's does not; both are the same group.
expect "getent group stnsempty (no members)" \
	"stnsempty:*:1003" \
	"$(getent group stnsempty | sed 's/:$//')"

echo
echo "== a large group forces the ERANGE-and-retry path =="
big=$(getent group stnsbig)
contains "getent group stnsbig has the first member" "stnsbulk000" "$big"
contains "getent group stnsbig has the last member" "stnsbulk299" "$big"
expect "getent group stnsbig has every member" "300" \
	"$(echo "$big" | sed 's/^[^:]*:[^:]*:[^:]*://' | tr ',' '\n' | grep -c stnsbulk)"

echo
echo "== local accounts still resolve through files =="
contains "getent passwd root" "root:" "$(getent passwd root)"
contains "getent group wheel" "wheel:" "$(getent group wheel)"

echo
echo "== a miss stays a miss =="
denies "getent passwd nosuchstnsuser fails" getent passwd nosuchstnsuser
denies "getent group nosuchstnsgroup fails" getent group nosuchstnsgroup
denies "getent passwd 65000 fails" getent passwd 65000

count_requests() {
	$PYTHON -c "
import json, urllib.request
print(len(json.load(urllib.request.urlopen('http://127.0.0.1:$PORT/v1/_requests'))))"
}

echo
echo "== names that could not be safe are refused without a request =="
before=$(count_requests)
getent passwd 'a&id=1001' >/dev/null 2>&1 || true
getent passwd 'a b' >/dev/null 2>&1 || true
getent group 'x/y' >/dev/null 2>&1 || true
after=$(count_requests)
expect "no request is made for an unsafe name" "$before" "$after"

echo
echo "== enumeration is still one request for the whole listing =="
before=$(count_requests)
getent passwd >/dev/null 2>&1 || true
after=$(count_requests)
expect "getent passwd with no key makes exactly one request" "1" "$((after - before))"

echo
echo "== a single lookup costs a single request =="
# getent(1) opens the database before looking a name up, so a set*ent() that
# fetched eagerly would double the cost of every single-name resolution.
before=$(count_requests)
getent passwd stnsuser >/dev/null 2>&1 || true
after=$(count_requests)
expect "getent passwd <name> makes exactly one request" "1" "$((after - before))"
before=$(count_requests)
getent group stnsops >/dev/null 2>&1 || true
after=$(count_requests)
expect "getent group <name> makes exactly one request" "1" "$((after - before))"

echo
echo "== enumeration =="
contains "getent passwd lists stnsuser" "stnsuser" "$(getent passwd)"
contains "getent passwd lists stnsdefault" "stnsdefault" "$(getent passwd)"
contains "getent passwd still lists root" "root" "$(getent passwd)"
contains "getent group lists stnsops" "stnsops" "$(getent group)"
contains "getent group lists stnsbig" "stnsbig" "$(getent group)"
expect "getent passwd lists every STNS user once" "6" \
	"$(getent passwd | grep -c '^stns')"

echo
echo "== supplementary groups (getgroupmembership) =="
groups_out=$(id -Gn stnsuser 2>&1 || true)
contains "id -Gn stnsuser has stnsgroup" "stnsgroup" "$groups_out"
contains "id -Gn stnsuser has stnsops" "stnsops" "$groups_out"
contains "id -Gn stnsuser has stnsextra" "stnsextra" "$groups_out"
lacks "id -Gn stnsuser does not have stnsbig" "stnsbig" "$groups_out"
lacks "id -Gn stnsdefault does not have stnsops" "stnsops" "$(id -Gn stnsdefault 2>&1 || true)"

echo
echo "== ssh key wrapper =="
keys=$("$SRCDIR/stns-key-wrapper" stnsuser)
contains "stns-key-wrapper prints the key" "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITESTKEY" "$keys"
expect "stns-key-wrapper prints both keys for stnskeys" "2" \
	"$("$SRCDIR/stns-key-wrapper" stnskeys | grep -c ssh-ed25519)"
expect "stns-key-wrapper on an unknown user prints nothing" "" \
	"$("$SRCDIR/stns-key-wrapper" nosuchstnsuser)"
expect "stns-key-wrapper on a user with no keys prints nothing" "" \
	"$("$SRCDIR/stns-key-wrapper" stnsdefault)"
denies "stns-key-wrapper rejects an unsafe name" "$SRCDIR/stns-key-wrapper" 'a&id=1'

echo
echo "== many keys, and a long one =="
# sshd asks for one user's keys at a time, so the wrapper has to hand back
# every one of them however big the answer gets, and hand each back exactly
# as it was given.
keys=$("$SRCDIR/stns-key-wrapper" stnsmanykeys)
expect "stns-key-wrapper prints all ten keys" "10" "$(echo "$keys" | grep -c '^ssh-')"
expect "the long key comes back whole" "760" \
	"$(echo "$keys" | grep '^ssh-rsa' | awk '{print length($0)}')"
contains "and with its comment" "a comment long enough to be awkward" "$keys"
expect "the keys are not run together" "10" "$(echo "$keys" | wc -l | tr -d ' ')"

echo
echo "== a record bigger than the first buffer =="
# 2000 characters of gecos and a 900 character home directory: libc hands the
# module a buffer, the module says ERANGE, libc comes back with a bigger one.
# The unit tests walk that path directly; this walks it through libc.
long=$(getent passwd stnslong)
expect "the oversized record resolves" "stnslong" "$(echo "$long" | cut -d: -f1)"
expect "its gecos survives in full" "2000" "$(echo "$long" | cut -d: -f5 | awk '{print length($0)}')"
expect "so does its home directory" "906" "$(echo "$long" | cut -d: -f6 | awk '{print length($0)}')"
expect "and it is the same by uid" "$long" "$(getent passwd 1006)"
contains "it is listed in an enumeration too" "stnslong" "$(getent passwd)"

echo
echo "== uid_shift / gid_shift =="
write_conf 'cache = false
uid_shift = 10000
gid_shift = 20000'

expect "shifted getent passwd stnsuser" \
	"stnsuser:*:11001:21001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"
expect "shifted getent passwd 11001" \
	"stnsuser:*:11001:21001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd 11001)"
expect "shifted getent group 21002" \
	"stnsops:*:21002:stnsuser" \
	"$(getent group 21002)"
denies "unshifted uid 1001 does not resolve" getent passwd 1001
contains "shifted id -Gn stnsuser has stnsops" "stnsops" "$(id -Gn stnsuser 2>&1 || true)"

echo
echo "== authentication =="
start_server --auth-token s3cr3t
write_conf 'cache = false
auth_token = "s3cr3t"'
succeeds "a correct auth_token is accepted" getent passwd stnsuser
write_conf 'cache = false
auth_token = "wrong"'
denies "a wrong auth_token is rejected" getent passwd stnsuser
write_conf 'cache = false'
denies "no auth_token at all is rejected" getent passwd stnsuser

start_server --basic stnsapi:hunter2
write_conf 'cache = false
user = "stnsapi"
password = "hunter2"'
succeeds "basic auth is sent" getent passwd stnsuser
write_conf 'cache = false
user = "stnsapi"
password = "wrong"'
denies "wrong basic auth is rejected" getent passwd stnsuser

start_server --require-header X-Api-Key:abc123
write_conf 'cache = false
[http_headers]
X-Api-Key = "abc123"'
succeeds "http_headers are sent" getent passwd stnsuser
write_conf 'cache = false'
denies "a missing required header is rejected" getent passwd stnsuser

echo
echo "== user agent =="
start_server
write_conf 'cache = false'
getent passwd stnsuser >/dev/null 2>&1 || true
ua=$($PYTHON -c "
import json, urllib.request
r = json.load(urllib.request.urlopen('http://127.0.0.1:$PORT/v1/_requests'))
print(r[-1]['user_agent'] if r else '')")
contains "the module identifies itself" "nss_stns/" "$ua"

echo
echo "== on-disk cache =="
write_conf 'cache = true
cache_ttl = 300'

expect "cached lookup, first pass" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"
succeeds "the cache directory was created" test -d "$CACHEDIR/0"
# With the server stopped the answer can now only come from the cache.
stop_server
expect "cached lookup survives the server going away" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"

echo
echo "== negative cache =="
start_server
write_conf 'cache = true
cache_ttl = 300
negative_cache_ttl = 300'
denies "an unknown user is a miss" getent passwd nosuchstnsuser
stop_server
denies "the miss is remembered while the server is down" getent passwd nosuchstnsuser
succeeds "the miss was cached as an empty file" \
	sh -c "find '$CACHEDIR/0' -size 0 -name '*nosuchstnsuser*' | grep -q ."

echo
echo "== an unprivileged caller =="
start_server
write_conf 'cache = true
cache_ttl = 300'
contains "root is given the password hash" '$6$stnssalt' "$(getent passwd stnshash)"
nobody_out=$(SHELL=/bin/sh su -m nobody -c "getent passwd stnsuser" 2>&1 || true)
contains "a non-root process resolves STNS users" "stnsuser:" "$nobody_out"
nobody_hash=$(SHELL=/bin/sh su -m nobody -c "getent passwd stnshash" 2>&1 || true)
contains "a non-root process resolves the hashed user too" "stnshash:" "$nobody_hash"
lacks "a non-root process never sees a password hash" '$6$' "$nobody_hash"

echo
echo "== query_wrapper =="
cat > "$WORKDIR/wrapper" <<WRAP
#!/bin/sh
exec curl -s "http://127.0.0.1:$PORT/v1/\$1"
WRAP
chmod +x "$WORKDIR/wrapper"
write_conf "cache = false
query_wrapper = \"$WORKDIR/wrapper\""
expect "lookups go through query_wrapper" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"

echo
echo "== cache-stnsd unix socket =="
start_unix_server
write_conf "cache = false
[cached]
enable = true
unix_socket = \"$WORKDIR/cached.sock\""
expect "lookups go over the unix socket" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"
expect "groups go over the unix socket" \
	"stnsops:*:1002:stnsuser" \
	"$(getent group stnsops)"

echo
echo "== over TLS =="
# The whole point of ssl_verify and [tls] is that a wrong or missing trust
# anchor stops the lookup.  A test that only proves "https works" would pass
# just as well with verification switched off, so each case below is paired
# with the one that must fail.
make_certs() {
	mkdir -p "$WORKDIR/tls"

	# Carry our own openssl.cnf.  req(1) insists on one and the three
	# systems keep theirs in different places, so relying on the default
	# makes this fail for a reason that has nothing to do with the module.
	# It has to name the extensions too: a "CA" certificate without
	# basicConstraints is not one, and every chain built on it is rejected.
	cat > "$WORKDIR/tls/openssl.cnf" <<CNF
[req]
distinguished_name = dn
[dn]
[ca]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
[server]
basicConstraints = critical,CA:FALSE
subjectAltName = IP:127.0.0.1, IP:::1
extendedKeyUsage = serverAuth
[client]
basicConstraints = critical,CA:FALSE
extendedKeyUsage = clientAuth
CNF

	# One CA, a server certificate for 127.0.0.1, and a client certificate.
	# Errors are deliberately not hidden: a certificate that fails to
	# generate should say why rather than fail the next step instead.
	openssl req -config "$WORKDIR/tls/openssl.cnf" \
		-x509 -extensions ca -newkey rsa:2048 -nodes -days 1 \
		-subj "/CN=nss_stns test CA" \
		-keyout "$WORKDIR/tls/ca.key" -out "$WORKDIR/tls/ca.pem"
	openssl req -config "$WORKDIR/tls/openssl.cnf" \
		-newkey rsa:2048 -nodes -subj "/CN=127.0.0.1" \
		-keyout "$WORKDIR/tls/server.key" -out "$WORKDIR/tls/server.csr"
	openssl x509 -req -in "$WORKDIR/tls/server.csr" -days 1 \
		-CA "$WORKDIR/tls/ca.pem" -CAkey "$WORKDIR/tls/ca.key" -CAcreateserial \
		-extfile "$WORKDIR/tls/openssl.cnf" -extensions server \
		-out "$WORKDIR/tls/server.pem"
	openssl req -config "$WORKDIR/tls/openssl.cnf" \
		-newkey rsa:2048 -nodes -subj "/CN=nss_stns client" \
		-keyout "$WORKDIR/tls/client.key" -out "$WORKDIR/tls/client.csr"
	openssl x509 -req -in "$WORKDIR/tls/client.csr" -days 1 \
		-CA "$WORKDIR/tls/ca.pem" -CAkey "$WORKDIR/tls/ca.key" -CAcreateserial \
		-extfile "$WORKDIR/tls/openssl.cnf" -extensions client \
		-out "$WORKDIR/tls/client.pem"

	# Readable by the unprivileged callers a lookup can come from.
	chmod 644 "$WORKDIR/tls"/*.pem "$WORKDIR/tls"/*.key
	openssl x509 -in "$WORKDIR/tls/ca.pem" -noout -text | grep -q "CA:TRUE" ||
		{ echo "the test CA is not a CA" >&2; exit 1; }
	openssl x509 -in "$WORKDIR/tls/server.pem" -noout -text |
		grep -A1 "Subject Alternative Name" | tail -1
}

start_tls_server() {
	stop_server
	$PYTHON "$MOCK" "$PORT" --tls "$WORKDIR/tls/server.pem" "$WORKDIR/tls/server.key" "$@" &
	server_pid=$!
	wait_for_port
}

make_certs
succeeds "the test CA and certificates were generated" test -s "$WORKDIR/tls/server.pem"

start_tls_server
write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\""
sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://127.0.0.1:$PORT/v1\"|" "$CONF"
expect "https with the right CA" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"

# No CA and verification on: this must fail, or ssl_verify means nothing.
write_conf 'cache = false
ssl_verify = true'
sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://127.0.0.1:$PORT/v1\"|" "$CONF"
denies "https with no trust anchor is refused" getent passwd stnsuser

# Same thing with verification off: this must succeed, or the knob means
# nothing either.
write_conf 'cache = false
ssl_verify = false'
sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://127.0.0.1:$PORT/v1\"|" "$CONF"
succeeds "ssl_verify = false accepts an untrusted certificate" getent passwd stnsuser

echo
echo "== TLS client certificates =="
start_tls_server --client-ca "$WORKDIR/tls/ca.pem"
write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\"
cert = \"$WORKDIR/tls/client.pem\"
key = \"$WORKDIR/tls/client.key\""
sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://127.0.0.1:$PORT/v1\"|" "$CONF"
expect "a client certificate is presented when one is demanded" \
	"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
	"$(getent passwd stnsuser)"

write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\""
sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://127.0.0.1:$PORT/v1\"|" "$CONF"
denies "without one the server refuses the connection" getent passwd stnsuser

start_server

echo
echo "== over IPv6 =="
# The module never parses api_endpoint; it hands the whole thing to curl.
# What is worth checking is that nothing on the way mangles a bracketed
# literal, and that two spellings of the same address behave the same way -
# ::1 and 0:0:0:0:0:0:0:1 are one address written twice.
if $PYTHON -c "
import socket, sys
s = socket.socket(socket.AF_INET6)
try:
	s.bind(('::1', 0))
except OSError:
	sys.exit(1)
" 2>/dev/null; then
	start_v6_server() {
		stop_server
		$PYTHON "$MOCK" "$PORT" --ipv6 "$@" &
		server_pid=$!
		i=0
		while [ $i -lt 60 ]; do
			$PYTHON -c "
import socket, sys
s = socket.socket(socket.AF_INET6)
s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('::1', $PORT)) == 0 else 1)
" 2>/dev/null && return 0
			i=$((i + 1))
			sleep 0.2
		done
		echo "the IPv6 mock server did not come up" >&2
		exit 1
	}

	start_v6_server
	for addr in "[::1]" "[0:0:0:0:0:0:0:1]"; do
		write_conf 'cache = false'
		sed -i.bak "s|^api_endpoint.*|api_endpoint = \"http://$addr:$PORT/v1\"|" "$CONF"
		expect "http over $addr" \
			"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
			"$(getent passwd stnsuser)"
	done

	# And the same over TLS: the certificate carries both loopback addresses,
	# so the only thing that changes is which one the endpoint names.
	start_v6_server --tls "$WORKDIR/tls/server.pem" "$WORKDIR/tls/server.key"
	write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\""
	sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://[::1]:$PORT/v1\"|" "$CONF"
	expect "https over [::1] with the right CA" \
		"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
		"$(getent passwd stnsuser)"

	write_conf 'cache = false
ssl_verify = true'
	sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://[::1]:$PORT/v1\"|" "$CONF"
	denies "https over [::1] with no trust anchor is refused" getent passwd stnsuser

	# A client certificate knows nothing about which IP version carried it -
	# CURLOPT_SSLCERT sits above all that - so this pair is here for
	# completeness rather than because it reaches new code.
	start_v6_server --tls "$WORKDIR/tls/server.pem" "$WORKDIR/tls/server.key" \
		--client-ca "$WORKDIR/tls/ca.pem"
	write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\"
cert = \"$WORKDIR/tls/client.pem\"
key = \"$WORKDIR/tls/client.key\""
	sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://[::1]:$PORT/v1\"|" "$CONF"
	expect "a client certificate is presented over [::1] too" \
		"stnsuser:*:1001:1001:STNS test user:/home/stnsuser:/bin/sh" \
		"$(getent passwd stnsuser)"

	write_conf "cache = false
ssl_verify = true
[tls]
ca = \"$WORKDIR/tls/ca.pem\""
	sed -i.bak "s|^api_endpoint.*|api_endpoint = \"https://[::1]:$PORT/v1\"|" "$CONF"
	denies "and without one the server refuses over [::1] as well" getent passwd stnsuser

	start_server
else
	# Not a silent skip: an image without a loopback ::1 cannot answer this
	# question, and the log should say so rather than imply it passed.
	echo "skip - this system has no ::1 to bind, IPv6 not exercised"
fi

echo
echo "== server unreachable =="
stop_server
write_conf 'cache = false
request_timeout = 2
request_retry = 0'

# Local accounts must keep working, and an STNS lookup must fail rather than
# hang, no matter that nothing is listening.
contains "getent passwd root with the API down" "root:" "$(getent passwd root)"
denies "getent passwd stnsuser fails while the API is down" getent passwd stnsuser
contains "getent group wheel with the API down" "wheel:" "$(getent group wheel)"
denies "id stnsuser fails while the API is down" id stnsuser

echo
echo "== a broken config does not break the system =="
write_raw_conf 'this is not valid toml = = ='
contains "local accounts survive a corrupt stns.conf" "root:" "$(getent passwd root)"
rm -f "$CONF"
contains "local accounts survive a missing stns.conf" "root:" "$(getent passwd root)"

echo
echo "$checks checks, $failures failures"
[ "$failures" -eq 0 ]
