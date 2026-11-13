#!/bin/sh
# Install an fwtrial bundle on the node (DL-24). Runs on the node, as root, from
# the unpacked bundle directory:
#
#   sh fwtrial-<version>/install.sh             install this release, switch to it, (re)start
#   sh /opt/fwtrial/current/install.sh --rollback   switch back to the previous release
#   sh /opt/fwtrial/current/install.sh --status     releases, current, service states
#
# Layout: /opt/fwtrial/releases/<version>/{bin,etc,systemd,install.sh,VERSION}
#         /opt/fwtrial/current -> releases/<version>   (switched atomically)
#         /opt/fwtrial/previous                        (version to roll back to)
#         /etc/fwtrial/*.conf                          (written only if missing)
#         /etc/systemd/system/fwtrial-*.service        (enabled: start at boot)
# /rec (recordings) is never touched.
set -eu

BASE=/opt/fwtrial
KEEP=3
UNITS="fwtrial-ptp4l.service fwtrial-nodeagent.service"
HERE=$(cd "$(dirname "$0")" && pwd)

die() { echo "install: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root"

current() { readlink "$BASE/current" 2>/dev/null | sed 's|^releases/||'; }

switch_to() {  # atomic symlink switch + service restart
	ln -sfn "releases/$1" "$BASE/current.new"
	mv -T "$BASE/current.new" "$BASE/current"
	systemctl daemon-reload
	systemctl enable $UNITS >/dev/null 2>&1
	systemctl restart $UNITS
	sleep 1
	for u in $UNITS; do
		echo "  $u: $(systemctl is-active "$u" || true)"
	done
	systemctl is-active --quiet fwtrial-nodeagent.service ||
		die "fwtrial-nodeagent not running: journalctl -u fwtrial-nodeagent"
}

status() {
	echo "current:  $(current || echo none)"
	echo "previous: $(cat "$BASE/previous" 2>/dev/null || echo none)"
	echo "releases: $(ls "$BASE/releases" 2>/dev/null | tr '\n' ' ')"
	for u in $UNITS; do
		echo "$u: $(systemctl is-enabled "$u" 2>/dev/null || true), $(systemctl is-active "$u" || true)"
	done
}

case "${1:-}" in
--status)
	status
	exit 0
	;;
--rollback)
	PREV=$(cat "$BASE/previous" 2>/dev/null || true)
	[ -n "$PREV" ] && [ -d "$BASE/releases/$PREV" ] || die "no previous release to roll back to"
	CUR=$(current)
	echo "rollback: $CUR -> $PREV"
	echo "$CUR" >"$BASE/previous"
	# units of the previous release (they may differ)
	cp "$BASE/releases/$PREV/systemd/"*.service /etc/systemd/system/
	switch_to "$PREV"
	echo "rolled back to $PREV"
	exit 0
	;;
"")
	;;
*)
	die "usage: install.sh [--rollback | --status]"
	;;
esac

VER=$(head -1 "$HERE/VERSION")
[ -n "$VER" ] && [ -x "$HERE/bin/nodeagent" ] || die "$HERE is not an fwtrial bundle"
echo "install: fwtrial $VER"

# 1. the release directory (copied whole, then renamed: never half-installed)
mkdir -p "$BASE/releases"
DEST="$BASE/releases/$VER"
if [ "$(current)" = "$VER" ]; then
	echo "  $VER is already current: reinstalling"
fi
rm -rf "$DEST.tmp"
cp -a "$HERE" "$DEST.tmp"
systemctl stop fwtrial-nodeagent.service 2>/dev/null || true
rm -rf "$DEST"
mv "$DEST.tmp" "$DEST"

# 2. configs: keep local edits, ship the defaults next to them
mkdir -p /etc/fwtrial
for f in nodeagent.conf ptp4l.conf; do
	cp "$DEST/etc/$f" "/etc/fwtrial/$f.default"
	if [ ! -e "/etc/fwtrial/$f" ]; then
		cp "$DEST/etc/$f" "/etc/fwtrial/$f"
		echo "  /etc/fwtrial/$f: installed default"
	elif ! cmp -s "$DEST/etc/$f" "/etc/fwtrial/$f"; then
		echo "  /etc/fwtrial/$f: kept (differs from the new default: see $f.default)"
	fi
done

# 3. units, remember the previous release, switch, start
cp "$DEST/systemd/"*.service /etc/systemd/system/
PREV=$(current || true)
if [ -n "$PREV" ] && [ "$PREV" != "$VER" ]; then
	echo "$PREV" >"$BASE/previous"
fi
switch_to "$VER"

# 4. keep the newest $KEEP releases (never current or previous)
KEEPV=$(cat "$BASE/previous" 2>/dev/null || true)
ls -1t "$BASE/releases" | tail -n +$((KEEP + 1)) | while read -r old; do
	[ "$old" = "$VER" ] || [ "$old" = "$KEEPV" ] || rm -rf "$BASE/releases/$old"
done

echo "installed $VER (rollback: sh $BASE/current/install.sh --rollback)"
