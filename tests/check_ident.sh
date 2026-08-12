#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
#
# Check that the ident line in stns.conf.example is really substituted.
#
# git archive expands $Format:...$ only for paths marked export-subst in
# .gitattributes, and nothing complains if that marking is lost - the released
# tarball would just quietly carry the raw placeholder for ever.  So build an
# archive the way a release does and look at what came out.

set -eu

SRCDIR=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-/tmp/nss_stns_ident}

cd "$SRCDIR"
if ! git rev-parse --git-dir >/dev/null 2>&1; then
	echo "skip - not a git checkout" >&2
	exit 0
fi

rm -rf "$WORK"
mkdir -p "$WORK"
git archive --format=tar HEAD stns.conf.example | tar -x -C "$WORK"

line=$(head -1 "$WORK/stns.conf.example")
rm -rf "$WORK"

echo "  $line"

rc=0
case "$line" in
*'$Format:'*)
	echo "FAIL - the ident line was not substituted; is the export-subst" >&2
	echo "       attribute still set for stns.conf.example?" >&2
	rc=1
	;;
esac
case "$line" in
'# $SNOWRABBIT: stns.conf,v '*' Exp $')
	;;
*)
	echo "FAIL - the ident line does not have the expected shape" >&2
	rc=1
	;;
esac

[ "$rc" -eq 0 ] && echo "ok   - the ident line is substituted on export"
exit "$rc"
