#!/usr/bin/env bash
# Regenerate the CBOR encode/decode code from the ICD (DL-03). Needs the laptop
# venv (zcbor, see tools-requirements.txt). Output is committed: the board build
# doesn't need Python.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ZCBOR=${ZCBOR:-$ROOT/.venv/bin/zcbor}
OUT=$ROOT/src/nodeagent/gen
cd "$OUT"
"$ZCBOR" code -c "$ROOT/icd/fwt-icd-v1.cddl" -b 64 --default-max-qty 512 \
	-t error ok-rep identity health arm-req arm-rep session-rep sessions-rep segments-rep \
	   lock-req lock-rep unlock-req \
	-d -e --oc icd.c --oh icd.h --oht icd_types.h --copy-sources
echo "generated in $OUT"
