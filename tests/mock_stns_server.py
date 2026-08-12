#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
#
# A stand-in for the STNS v2 API, used by tests/integration.sh so the test
# suite does not need a Go toolchain to build the real server.
#
# It implements just the surface nss_stns talks to:
#     GET /v1/users            GET /v1/users?name=  GET /v1/users?id=
#     GET /v1/groups           GET /v1/groups?name= GET /v1/groups?id=
# plus the User-/Group-Highest-Id headers the module uses to skip lookups.
#
# The /v1 prefix is optional, because with [cached] enable = true the module
# talks to cache-stnsd's socket and the daemon is the one that knows about
# API versions.
#
# Beyond serving data it also records what it was asked, and can require
# credentials, so the tests can assert that the module really does send the
# auth_token, basic auth and http_headers it was configured with.
#
# usage: mock_stns_server.py [--auth-token T] [--basic user:pass]
#                            [--require-header K:V] [--unix PATH | PORT]
#                            [--tls CERT KEY] [--client-ca CA] [--ipv6]

import base64
import json
import os
import socket
import socketserver
import ssl
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

USERS = [
    {
        "id": 1001,
        "name": "stnsuser",
        "group_id": 1001,
        "directory": "/home/stnsuser",
        "shell": "/bin/sh",
        "gecos": "STNS test user",
        "keys": ["ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITESTKEY stnsuser"],
        "password": "",
    },
    {
        # Empty shell and directory exercise the module's defaults.
        "id": 1002,
        "name": "stnsdefault",
        "group_id": 1001,
        "directory": "",
        "shell": "",
        "gecos": "",
        "keys": [],
        "password": "",
    },
    {
        # Has a password hash, so the tests can check that it reaches root and
        # nobody else.
        "id": 1004,
        "name": "stnshash",
        "group_id": 1001,
        "directory": "/home/stnshash",
        "shell": "/bin/sh",
        "gecos": "",
        "keys": [],
        "password": "$6$stnssalt$0123456789abcdef",
    },
    {
        # Ten keys, one of them the length of an RSA 4096 key with a long
        # comment.  sshd asks for these one user at a time, so the wrapper
        # has to hand back every one of them intact however big the answer
        # gets.
        "id": 1005,
        "name": "stnsmanykeys",
        "group_id": 1001,
        "directory": "/home/stnsmanykeys",
        "shell": "/bin/sh",
        "gecos": "",
        "keys": (
            ["ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIKEY%03d stnsmanykeys" % i for i in range(9)]
            + ["ssh-rsa " + ("B" * 716) + " a comment long enough to be awkward"]
        ),
        "password": "",
    },
    {
        # A record far larger than any first-guess buffer, so that the
        # ERANGE-and-retry path is walked by libc itself rather than only by
        # the unit tests.
        "id": 1006,
        "name": "stnslong",
        "group_id": 1001,
        "directory": "/home/" + ("d" * 900),
        "shell": "/bin/sh",
        "gecos": "g" * 2000,
        "keys": [],
        "password": "",
    },
    {
        # Two keys, to check the wrapper prints all of them.
        "id": 1003,
        "name": "stnskeys",
        "group_id": 1001,
        "directory": "/home/stnskeys",
        "shell": "/bin/sh",
        "gecos": "",
        "keys": [
            "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIKEYONE stnskeys",
            "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIKEYTWO stnskeys",
        ],
        "password": "",
    },
]

# A group whose member list is far larger than any first-guess buffer, so that
# the ERANGE-and-retry path is exercised for real rather than only in unit
# tests.  Every member name is distinct and predictable.
BIG_MEMBERS = ["stnsbulk%03d" % i for i in range(300)]

GROUPS = [
    {"id": 1001, "name": "stnsgroup", "users": ["stnsuser", "stnsdefault"]},
    {"id": 1002, "name": "stnsops", "users": ["stnsuser"]},
    {"id": 1003, "name": "stnsempty", "users": []},
    {"id": 1004, "name": "stnsbig", "users": BIG_MEMBERS},
    # stnsuser is in this one too, so getgroupmembership has to merge three.
    {"id": 1005, "name": "stnsextra", "users": ["stnsuser"]},
]

OPTS = {"auth_token": None, "basic": None, "require_header": None}
REQUESTS = []
LOCK = threading.Lock()


def select(items, query):
    if "name" in query:
        return [i for i in items if i["name"] == query["name"][0]]
    if "id" in query:
        try:
            wanted = int(query["id"][0])
        except ValueError:
            return None
        return [i for i in items if i["id"] == wanted]
    return items


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("mock_stns: " + (fmt % args) + "\n")

    def authorized(self):
        if OPTS["auth_token"] is not None:
            if self.headers.get("Authorization") != "token " + OPTS["auth_token"]:
                return False
        if OPTS["basic"] is not None:
            want = "Basic " + base64.b64encode(OPTS["basic"].encode()).decode()
            if self.headers.get("Authorization") != want:
                return False
        if OPTS["require_header"] is not None:
            key, _, value = OPTS["require_header"].partition(":")
            if self.headers.get(key) != value:
                return False
        return True

    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        path = url.path
        if path.startswith("/v1"):
            path = path[3:] or "/"

        # The recording endpoint is the tests' own: never authenticated, and
        # deliberately not recorded, so that asking how many requests were made
        # does not itself count as one.
        if path == "/_requests":
            with LOCK:
                body = json.dumps(REQUESTS).encode()
            self.respond(200, body)
            return

        with LOCK:
            REQUESTS.append(
                {
                    "path": self.path,
                    "authorization": self.headers.get("Authorization"),
                    "user_agent": self.headers.get("User-Agent"),
                    "x_api_key": self.headers.get("X-Api-Key"),
                }
            )

        if not self.authorized():
            self.respond(401, b'{"error":"unauthorized"}')
            return

        if path == "/users":
            items = select(USERS, query)
        elif path == "/groups":
            items = select(GROUPS, query)
        elif path in ("/status", "/"):
            self.respond(200, b"ok", content_type="text/plain")
            return
        else:
            self.respond(404, b'{"error":"not found"}')
            return

        if items is None:
            self.respond(400, b'{"error":"bad request"}')
            return
        if not items:
            # The real server answers 404 for an empty result set, which is
            # what the module turns into a negative cache entry.
            self.respond(404, b'{"error":"not found"}')
            return

        self.respond(200, json.dumps(items).encode())

    def respond(self, code, body, content_type="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("User-Highest-Id", str(max(u["id"] for u in USERS)))
        self.send_header("User-Lowest-Id", str(min(u["id"] for u in USERS)))
        self.send_header("Group-Highest-Id", str(max(g["id"] for g in GROUPS)))
        self.send_header("Group-Lowest-Id", str(min(g["id"] for g in GROUPS)))
        self.end_headers()
        self.wfile.write(body)


class ThreadingHTTPServer6(ThreadingHTTPServer):
    """The same server on ::1.  Nothing else changes; the module hands the
    whole endpoint to curl, so what is being checked is that a bracketed
    literal survives the trip."""

    address_family = socket.AF_INET6


class UnixHTTPServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    """Serves the same handler over a unix socket, standing in for cache-stnsd."""

    allow_reuse_address = True
    daemon_threads = True
    server_name = "unix"
    server_port = 0

    def get_request(self):
        conn, _ = super().get_request()
        # BaseHTTPRequestHandler wants a client address it can format.
        return conn, ("unix", 0)


def tls_context(cert, key, client_ca):
    """A server context, optionally demanding a client certificate."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    if client_ca is not None:
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(client_ca)
    return ctx


def main():
    args = sys.argv[1:]
    unix_path = None
    port = 1104
    tls = None
    client_ca = None
    ipv6 = False

    while args:
        arg = args.pop(0)
        if arg == "--auth-token":
            OPTS["auth_token"] = args.pop(0)
        elif arg == "--tls":
            tls = (args.pop(0), args.pop(0))
        elif arg == "--client-ca":
            client_ca = args.pop(0)
        elif arg == "--ipv6":
            ipv6 = True
        elif arg == "--basic":
            OPTS["basic"] = args.pop(0)
        elif arg == "--require-header":
            OPTS["require_header"] = args.pop(0)
        elif arg == "--unix":
            unix_path = args.pop(0)
        else:
            port = int(arg)

    if unix_path is not None:
        if os.path.exists(unix_path):
            os.unlink(unix_path)
        server = UnixHTTPServer(unix_path, Handler)
        os.chmod(unix_path, 0o666)
        sys.stderr.write("mock_stns: listening on unix:%s\n" % unix_path)
    elif ipv6:
        server = ThreadingHTTPServer6(("::1", port), Handler)
        if tls is not None:
            server.socket = tls_context(tls[0], tls[1], client_ca).wrap_socket(
                server.socket, server_side=True
            )
        scheme = "https" if tls is not None else "http"
        sys.stderr.write("mock_stns: listening on %s://[::1]:%d\n" % (scheme, port))
    else:
        server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        if tls is not None:
            server.socket = tls_context(tls[0], tls[1], client_ca).wrap_socket(
                server.socket, server_side=True
            )
        scheme = "https" if tls is not None else "http"
        sys.stderr.write("mock_stns: listening on %s://127.0.0.1:%d\n" % (scheme, port))

    server.serve_forever()


if __name__ == "__main__":
    main()
