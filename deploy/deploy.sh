#!/usr/bin/env bash
# One command from a checkout to a running node (DL-24):
#
#   deploy/deploy.sh root@10.42.0.132
#
# make-bundle.sh -> copy the tarball to the node -> install.sh there (release
# directory, symlink switch, systemd units enabled and restarted) -> check from
# here that GET /v1/identity answers with the new version.
set -euo pipefail

NODE=${1:?usage: deploy/deploy.sh <user@node>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOST=${NODE#*@}
PORT=${PORT:-8080}

TAR=$("$ROOT/deploy/make-bundle.sh" | tail -1)
NAME=$(basename "$TAR" -aarch64.tar.gz)
VER=${NAME#fwtrial-}

echo "== copy $(basename "$TAR") to $NODE"
scp -q "$TAR" "$NODE:/tmp/"

echo "== install on $NODE"
ssh -o BatchMode=yes "$NODE" \
	"set -e; cd /tmp; rm -rf '$NAME'; tar xzf '$(basename "$TAR")'; \
	 sh '$NAME/install.sh'; rm -rf '$NAME' '$(basename "$TAR")'"

echo "== check http://$HOST:$PORT/v1/identity"
PY=$ROOT/.venv/bin/python
for _ in $(seq 1 20); do
	if ID=$(curl -s --max-time 2 "http://$HOST:$PORT/v1/identity" | "$PY" -I "$ROOT/tools/icd-json" to-json 2>/dev/null) &&
		echo "$ID" | grep -q "\"version\": \"$VER\""; then
		echo "$ID"
		echo "DEPLOYED $VER to $NODE (starts at boot; rollback: ssh $NODE sh /opt/fwtrial/current/install.sh --rollback)"
		exit 0
	fi
	sleep 0.5
done
echo "deploy: $NODE does not answer with version $VER (ssh $NODE journalctl -u fwtrial-nodeagent)" >&2
exit 1
