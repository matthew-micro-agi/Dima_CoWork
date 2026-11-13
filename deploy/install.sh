#!/bin/sh
# Install an fwtrial bundle on the node (DL-24). Runs on the node, as root, from
# the unpacked bundle directory:
#
#   sh fwtrial-<version>/install.sh             install this release, switch to it, (re)start
#   sh /opt/fwtrial/current/install.sh --rollback   switch back to the previous release
#   sh /opt/fwtrial/current/install.sh --status     releases, current, service states
#   sh /opt/fwtrial/current/install.sh --uninstall  remove everything of ours except /rec;
#                                                   refused while a session runs
#
# Layout: /opt/fwtrial/releases/<version>/{bin,etc,systemd,install.sh,VERSION}
#         /opt/fwtrial/current -> releases/<version>   (switched atomically)
#         /opt/fwtrial/previous                        (version to roll back to)
#         /etc/fwtrial/*.conf                          (written only if missing)
#         /etc/systemd/system/fwtrial-*.service        (enabled: start at boot)
#         /var/log/fwtrial, /run/fwtrial               (made by systemd for the service)
# /rec (recordings) is never touched, not even by --uninstall.
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

# Remove the installation: services, units (and drop-ins), releases, configs,
# logs. Keeps /rec. Refused while trigd or nodecap runs (a session is armed or
# a check script is running): end it first, nothing is cut off midway.
uninstall() {
	busy=$(for f in /proc/[0-9]*/comm; do
		c=$(cat "$f" 2>/dev/null) || continue
		case "$c" in trigd|nodecap) p=${f#/proc/}; echo "$c (pid ${p%/comm})" ;; esac
	done)
	if [ -n "$busy" ]; then
		echo "uninstall: refused, running:" >&2
		echo "$busy" | sed 's/^/  /' >&2
		die "end the session first (disarm via the control interface, or Ctrl-C / kill the check run); then retry"
	fi

	if [ ! -e "$BASE" ] && ! ls /etc/systemd/system/fwtrial-*.service >/dev/null 2>&1; then
		echo "uninstall: nothing installed"
		return 0
	fi
	echo "uninstall: fwtrial $(current || echo '(no current release)')"
	cd /  # this script may live in the directory being removed

	# 1. stop + disable; the nodeagent unit's ExecStopPost turns the trigger output off
	systemctl disable --now $UNITS >/dev/null 2>&1 || true
	# and once more directly, in case the service wasn't running (DL-14)
	if [ -x "$BASE/current/bin/trigd" ]; then
		"$BASE/current/bin/trigd" --disarm-only >/dev/null 2>&1 || true
	fi

	# 2. units, drop-ins (systemctl edit), systemd's state
	for u in $UNITS; do
		rm -f "/etc/systemd/system/$u"
		rm -rf "/etc/systemd/system/$u.d"
	done
	systemctl daemon-reload
	systemctl reset-failed $UNITS >/dev/null 2>&1 || true

	# 3. releases, configs, logs, runtime sockets
	rm -rf "$BASE" /etc/fwtrial /var/log/fwtrial /run/fwtrial

	# 4. check
	left=$(systemctl list-unit-files 'fwtrial*' --no-legend 2>/dev/null || true)
	for d in "$BASE" /etc/fwtrial /var/log/fwtrial /run/fwtrial; do
		if [ -e "$d" ]; then left="$left $d"; fi
	done
	[ -z "$left" ] || die "left behind: $left"
	echo "  removed: $UNITS (+ drop-ins), $BASE, /etc/fwtrial, /var/log/fwtrial, /run/fwtrial"
	echo "  kept:    /rec (recordings)"
	echo "uninstalled (install again: deploy/deploy.sh <user@node>)"
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
--uninstall)
	uninstall
	exit 0
	;;
"")
	;;
*)
	die "usage: install.sh [--rollback | --status | --uninstall]"
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
