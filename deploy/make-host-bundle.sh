#!/usr/bin/env bash
# Build the operator's host bundle: everything to deploy a node, record and
# check, without build tools (no compiler, SDK, Python).
#
#   deploy/make-host-bundle.sh
#   -> dist/fwtrial-host-<version>-x86_64.tar.gz
#        fwtrial-host-<version>/
#          README                          operator commands
#          VERSION
#          deploy.sh, install.sh           deploy / uninstall a node
#          fwtrial-<version>-aarch64.tar.gz   the node bundle (make-bundle.sh)
#          bin/fwt-rec bin/sei-dump bin/fwt-doctor
#
# Runs on a developer laptop (fwt-doctor host-dev). The host programs are built
# here (Ubuntu 24.04, x86_64) and need on the operator laptop: openssh-client,
# curl, ffmpeg, libcurl4t64 (checked by bin/fwt-doctor host).
# Prints the tarball path as the last line.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build-host
DIST=$ROOT/dist

echo "== laptop build (fwt-rec, sei-dump)" >&2
cmake -S "$ROOT" -B "$BUILD" >&2
cmake --build "$BUILD" -j --target fwt-rec sei-dump >&2

echo "== node bundle" >&2
NODE_TAR=$("$ROOT/deploy/make-bundle.sh" | tail -1)

VER=$(sed -n 's/^#define FWT_VERSION "\(.*\)"/\1/p' "$BUILD/generated/fwt_version.h")
NODE_VER=$(basename "$NODE_TAR" -aarch64.tar.gz)
NODE_VER=${NODE_VER#fwtrial-}
[ -n "$VER" ] && [ "$VER" = "$NODE_VER" ] ||
	{ echo "make-host-bundle: host $VER and node $NODE_VER versions differ" >&2; exit 1; }
case "$VER" in *-dirty) echo "make-host-bundle: WARNING: uncommitted changes ($VER)" >&2 ;; esac

NAME=fwtrial-host-$VER
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
OUT=$STAGE/$NAME
mkdir -p "$OUT/bin"

echo "== stage $NAME" >&2
cp "$BUILD/src/tools/fwt-rec/fwt-rec" "$BUILD/src/tools/sei-dump/sei-dump" \
	"$ROOT/tools/fwt-doctor" "$OUT/bin/"
cp "$ROOT/deploy/deploy.sh" "$ROOT/deploy/install.sh" "$NODE_TAR" "$OUT/"
cp "$ROOT/deploy/host-README" "$OUT/README"
chmod 755 "$OUT/bin/"* "$OUT/deploy.sh"
{
	echo "$VER"
	echo "commit $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
	echo "built $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(. /etc/os-release && echo "$PRETTY_NAME") $(uname -m)"
} >"$OUT/VERSION"

mkdir -p "$DIST"
TAR=$DIST/$NAME-x86_64.tar.gz
tar -C "$STAGE" --owner=0 --group=0 -czf "$TAR" "$NAME"
echo "== $(du -h "$TAR" | cut -f1)  $TAR" >&2
echo "$TAR"
