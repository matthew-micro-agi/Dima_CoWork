#!/usr/bin/env bash
# One command to a running node (DL-24):
#
#   deploy/deploy.sh root@10.42.0.132                       checkout: build the bundle, install it
#   deploy/deploy.sh root@10.42.0.132 --bundle FILE         install a prebuilt node bundle
#   deploy/deploy.sh root@10.42.0.132 --uninstall           remove it again (all but /rec)
#
# In a host bundle (no build tools): ./deploy.sh root@10.42.0.132 installs the
# node bundle shipped next to it.
#
# pre-check (fwt-doctor: link) -> [make-bundle.sh] -> copy the tarball to the
# node -> install.sh there (release directory, symlink switch, systemd units
# enabled and restarted) -> check from here that the node answers with the new
# version (fwt-rec --info). No Python needed.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
USAGE="usage: $0 <user@node> [--bundle FILE | --uninstall]"
NODE=${1:?$USAGE}
shift
MODE=install
TAR=""
case "${1:-}" in
"") ;;
--uninstall) MODE=uninstall ;;
--bundle) TAR=${2:?$USAGE} ;;
*) echo "$USAGE" >&2; exit 2 ;;
esac
HOST=${NODE#*@}
PORT=${PORT:-8080}

first() {  # first existing executable among the arguments, or nothing (status 0: set -e)
	local c
	for c in "$@"; do
		[ -x "$c" ] && { echo "$c"; return 0; }
	done
	return 0
}
FWT_REC=$(first "$HERE/bin/fwt-rec" "$HERE/../build-host/src/tools/fwt-rec/fwt-rec")
DOCTOR=$(first "$HERE/bin/fwt-doctor" "$HERE/../tools/fwt-doctor")

if [ -n "$DOCTOR" ] && ! FWT_DOCTOR_QUIET=1 "$DOCTOR" --only link "$NODE"; then
	echo "deploy: fix the link to $NODE first (above)" >&2
	exit 1
fi

if [ "$MODE" = uninstall ]; then
	# our install.sh: works whatever is (or isn't) installed there
	echo "== uninstall on $NODE"
	scp -q "$HERE/install.sh" "$NODE:/tmp/fwtrial-install.sh"
	ssh -o BatchMode=yes "$NODE" \
		'sh /tmp/fwtrial-install.sh --uninstall; rc=$?; rm -f /tmp/fwtrial-install.sh; exit $rc'
	exit
fi

if [ -z "$TAR" ]; then
	if [ -x "$HERE/make-bundle.sh" ]; then
		TAR=$("$HERE/make-bundle.sh" | tail -1)
	else
		# host bundle: the node bundle shipped next to this script
		set -- "$HERE"/fwtrial-*-aarch64.tar.gz
		[ $# -eq 1 ] && [ -f "$1" ] ||
			{ echo "deploy: expected one fwtrial-*-aarch64.tar.gz in $HERE (or give --bundle FILE)" >&2; exit 1; }
		TAR=$1
	fi
fi
case "$(basename "$TAR")" in
fwtrial-*-aarch64.tar.gz) [ -f "$TAR" ] || { echo "deploy: $TAR not found" >&2; exit 1; } ;;
*) echo "deploy: $TAR is not a node bundle (fwtrial-<version>-aarch64.tar.gz)" >&2; exit 1 ;;
esac
NAME=$(basename "$TAR" -aarch64.tar.gz)
VER=${NAME#fwtrial-}

echo "== copy $(basename "$TAR") to $NODE"
scp -q "$TAR" "$NODE:/tmp/"

echo "== install on $NODE"
ssh -o BatchMode=yes "$NODE" \
	"set -e; cd /tmp; rm -rf '$NAME'; tar xzf '$(basename "$TAR")'; \
	 sh '$NAME/install.sh'; rm -rf '$NAME' '$(basename "$TAR")'"

echo "== check $HOST:$PORT answers with version $VER"
for _ in $(seq 1 20); do
	if [ -n "$FWT_REC" ]; then
		if INFO=$("$FWT_REC" "$HOST:$PORT" --info 2>/dev/null) && echo "$INFO" | grep -qx "version: $VER"; then
			echo "$INFO"
			echo "DEPLOYED $VER to $NODE (starts at boot; rollback: ssh $NODE sh /opt/fwtrial/current/install.sh --rollback)"
			exit 0
		fi
	# no fwt-rec here: the version string appears as is in the CBOR reply
	elif curl -s --max-time 2 "http://$HOST:$PORT/v1/identity" | grep -aqF "$VER"; then
		echo "DEPLOYED $VER to $NODE (checked with curl; build fwt-rec for a full check)"
		exit 0
	fi
	sleep 0.5
done
echo "deploy: $NODE does not answer with version $VER (ssh $NODE journalctl -u fwtrial-nodeagent)" >&2
exit 1
