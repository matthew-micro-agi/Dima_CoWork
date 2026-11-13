#!/usr/bin/env bash
# Build the node bundle (DL-24): cross-build with the TI SDK, then pack
#   dist/fwtrial-<version>-aarch64.tar.gz
#     fwtrial-<version>/bin/        trigd nodecap nodeagent rec-tool trig-listen fwt-info
#     fwtrial-<version>/systemd/    fwtrial-nodeagent.service fwtrial-ptp4l.service
#     fwtrial-<version>/etc/        nodeagent.conf ptp4l.conf (defaults)
#     fwtrial-<version>/install.sh  run on the node (see deploy/install.sh)
#     fwtrial-<version>/VERSION     version, git commit, build date
# Prints the tarball path as the last line. Works offline; hand the tarball to anyone.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build-board
DIST=$ROOT/dist

echo "== cross-build (TI SDK: cmake/j722s-sdk.cmake)" >&2
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/j722s-sdk.cmake" >&2
cmake --build "$BUILD" -j >&2

VER=$(sed -n 's/^#define FWT_VERSION "\(.*\)"/\1/p' "$BUILD/generated/fwt_version.h")
[ -n "$VER" ] || { echo "make-bundle: no version" >&2; exit 1; }
case "$VER" in *-dirty) echo "make-bundle: WARNING: uncommitted changes ($VER)" >&2 ;; esac

NAME=fwtrial-$VER
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
OUT=$STAGE/$NAME

echo "== stage $NAME" >&2
cmake --install "$BUILD" --prefix "$OUT" >&2
mkdir -p "$OUT/systemd" "$OUT/etc"
cp "$ROOT"/deploy/systemd/*.service "$OUT/systemd/"
cp "$ROOT"/deploy/etc/*.conf "$OUT/etc/"
cp "$ROOT/deploy/install.sh" "$OUT/install.sh"
chmod 755 "$OUT/install.sh"
{
	echo "$VER"
	echo "commit $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
	echo "built $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"$OUT/VERSION"

for b in trigd nodecap nodeagent rec-tool; do
	[ -x "$OUT/bin/$b" ] || { echo "make-bundle: missing bin/$b" >&2; exit 1; }
done

mkdir -p "$DIST"
TAR=$DIST/$NAME-aarch64.tar.gz
tar -C "$STAGE" --owner=0 --group=0 -czf "$TAR" "$NAME"
echo "== $(du -h "$TAR" | cut -f1)  $TAR" >&2
echo "$TAR"
