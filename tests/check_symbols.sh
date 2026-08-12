#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# Assert that the module exports nss_module_register and nothing else of ours.
#
# This is not a nicety.  libc finds the module through
# dlsym(handle, "nss_module_register"); if that one symbol is hidden the module
# loads, registers nothing and every lookup silently falls through to the next
# source, which looks exactly like a working system with an empty directory.
# Conversely, anything else we export is loaded into every process on the
# machine and can collide with the host program's own symbols.

set -eu

module=${1:?usage: check_symbols.sh <module.so>}

if ! syms=$(nm -D --defined-only "$module" 2>/dev/null); then
	syms=$(nm -g --defined-only "$module")
fi

echo "$syms"

if ! echo "$syms" | grep -q '[[:space:]]nss_module_register$'; then
	echo "FAIL: $module does not export nss_module_register" >&2
	exit 1
fi
echo "ok   - nss_module_register is exported"

# Symbols the toolchain always emits; everything else of ours must stay hidden.
leaked=$(echo "$syms" | awk '{print $NF}' |
	grep -v '^nss_module_register$' |
	grep -v '^_' |
	grep -v '^__' || true)

if [ -n "$leaked" ]; then
	echo "FAIL: $module leaks symbols:" >&2
	echo "$leaked" >&2
	exit 1
fi
echo "ok   - no other symbols are exported"
