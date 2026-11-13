#!/usr/bin/env bash
# End-to-end check of the control interface (nodeagent, icd/fwt-icd-v1.cddl),
# driven from the laptop with curl + tools/icd-json (CBOR <-> JSON):
#   identity; arm (busy on a second arm); lock while recording; health; fetch
#   while recording (open segment reported); disarm; sessions, segments; fetch
#   last 5 s and the whole session -> sei-dump; unlock; error replies.
#
#   tests/nodeagent-check.sh host               laptop: nodeagent on port 18080, temp /rec,
#                                               trigd on the system clock, x265 960x600
#   tests/nodeagent-check.sh dev <user@node>    board: nodeagent on port 8080 with /rec,
#                                               Wave5, PTP clock; driven over the network
#                                               e.g.    tests/nodeagent-check.sh dev root@10.42.0.132
#
# Prints PASS/FAIL per step; exit code 0 only if every step passed.
set -u

MODE=${1:-}
NODE=${2:-}
NAME=nodeagent-check
. "$(dirname "$0")/check-lib.sh"

PY="$ROOT/.venv/bin/python"
J="$ROOT/tools/icd-json"
SEI_DUMP="$ROOT/build-host/src/tools/sei-dump/sei-dump"

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

tojson() { "$PY" -I "$J" to-json; }
cbor() { "$PY" -I "$J" to-cbor "$1"; }

# req <name> <method> <path> [json body]: body -> $LOG/<name>.json, status -> $LOG/<name>.code
req() {
	local name=$1 method=$2 path=$3 body=${4:-}
	if [ -n "$body" ]; then
		cbor "$body" | curl -s -o "$LOG/$name.cbor" -w '%{http_code}' -X "$method" \
			-H 'Content-Type: application/cbor' --data-binary @- "$URL$path" \
			>"$LOG/$name.code"
	else
		curl -s -o "$LOG/$name.cbor" -w '%{http_code}' -X "$method" "$URL$path" \
			>"$LOG/$name.code"
	fi
	tojson <"$LOG/$name.cbor" >"$LOG/$name.json" 2>/dev/null
}

# expect <name> <http status> <grep pattern on the JSON>
expect() {
	local code
	code=$(cat "$LOG/$1.code")
	if [ "$code" = "$2" ] && grep -q -- "$3" "$LOG/$1.json"; then
		pass "$1: HTTP $code, $3"
	else
		fail "$1: HTTP $code (want $2), $(head -c 200 "$LOG/$1.json") (want $3)"
	fi
}

# fetch <name> <query>: stream -> $LOG/<name>.h265, headers -> $LOG/<name>.hdr
fetch() {
	curl -s -D "$LOG/$1.hdr" -o "$LOG/$1.h265" "$URL/v1/fetch?$2"
}

hdr() { sed -n "s/^$2: *\([^\r]*\).*/\1/Ip" "$LOG/$1.hdr"; }

scenario() {
	local S S2 lock n
	S=$(new_session)
	S2=$(new_session)

	echo "== identity, arm"
	req identity GET /v1/identity
	expect identity 200 '"icd": 1'
	req arm POST /v1/arm "{\"session\": $S, \"fps\": 30}"
	expect arm 200 "\"session\": $S"
	req arm-again POST /v1/arm "{\"session\": $S2, \"fps\": 30}"
	expect arm-again 409 '"code": "busy"'

	echo "== while recording (8 s)"
	sleep 8
	req lock POST /v1/lock "{\"session\": $S, \"from_idx\": 30, \"to_idx\": 400, \"ttl_s\": 600, \"reason\": \"check\"}"
	expect lock 200 '"lock_id"'
	lock=$(sed -n 's/.*"lock_id": \([0-9]*\).*/\1/p' "$LOG/lock.json")
	req health-rec GET /v1/health
	expect health-rec 200 '"state": "recording"'
	fetch live "session=$S&from_idx=0&to_idx=10000"
	[ "$(hdr live X-Fwt-Open-Segment)" = "not-included" ] && [ "$(hdr live X-Fwt-From)" = "0" ] &&
		pass "fetch while recording: closed segments now, open segment reported (to $(hdr live X-Fwt-To))" ||
		fail "fetch while recording (see $LOG/live.hdr)"

	echo "== disarm, list"
	sleep 2
	req disarm POST /v1/disarm
	expect disarm 200 "\"session\": $S"
	req health-idle GET /v1/health
	expect health-idle 200 'nodecap exit 0'
	req sessions GET /v1/sessions
	expect sessions 200 "\"session\": $S, \"status\": \"complete\""
	req segments GET "/v1/segments?session=$S"
	expect segments 200 '"locked": true'

	echo "== fetch and check the bytes"
	fetch last5 "session=$S&last_s=5"
	"$SEI_DUMP" -q "$LOG/last5.h265" >"$LOG/last5.sei" 2>&1 &&
		pass "fetch last 5 s: from $(hdr last5 X-Fwt-From) (keyframe), sei-dump clean" ||
		fail "fetch last 5 s: sei-dump (see $LOG/last5.sei)"
	fetch all "session=$S&from_idx=0"
	n=$(( $(hdr all X-Fwt-To) - $(hdr all X-Fwt-From) + 1 ))
	"$SEI_DUMP" -q --frames "$n" "$LOG/all.h265" >"$LOG/all.sei" 2>&1 &&
		[ "$(hdr all X-Fwt-Missing)" = "none" ] &&
		pass "fetch whole session: $n frames, nothing missing, sei-dump clean (pixels too)" ||
		fail "fetch whole session (see $LOG/all.hdr, $LOG/all.sei)"

	echo "== unlock, errors"
	req unlock POST /v1/unlock "{\"lock_id\": $lock}"
	expect unlock 200 '"icd": 1'
	req unlock-again POST /v1/unlock "{\"lock_id\": $lock}"
	expect unlock-again 404 '"code": "no-lock"'
	req arm-reuse POST /v1/arm "{\"session\": $S, \"fps\": 30}"
	expect arm-reuse 409 '"code": "session-exists"'
	req bad-query GET "/v1/fetch?session=$S&from_idx=abc"
	expect bad-query 400 '"code": "bad-request"'
	req no-session GET "/v1/fetch?session=1&from_idx=0"
	expect no-session 404 '"code": "no-data"'
	req too-long GET "/v1/fetch?session=$S&from_idx=0&to_idx=999999"
	expect too-long 400 '"code": "range-too-long"'
	printf 'garbage' | curl -s -o "$LOG/garbage.cbor" -w '%{http_code}' -X POST \
		--data-binary @- "$URL/v1/lock" >"$LOG/garbage.code"
	tojson <"$LOG/garbage.cbor" >"$LOG/garbage.json"
	expect garbage 400 '"code": "bad-request"'
	req no-path GET /v1/nothing
	expect no-path 404 '"code": "not-found"'
	req disarm-idle POST /v1/disarm
	expect disarm-idle 409 '"code": "not-armed"'
}

# fwt-rec (laptop client, C): one recording through the whole interface
# (identity, health, arm, wait on the node clock, disarm, sessions, segments,
# fetch in 2 s parts), file checked with sei-dump, sidecar JSON parsed.
fwt_rec_run() {
	local F="$ROOT/build-host/src/tools/fwt-rec/fwt-rec" out="$LOG/fwt-rec.h265" rc
	echo "== fwt-rec: 5 s recording, fetched in 2 s parts"
	"$F" 127.0.0.1:18080 --duration 5 --fetch-s 2 --out "$out" -q >"$LOG/fwt-rec.log" 2>&1
	rc=$?
	expect_rc "fwt-rec" $rc 0
	grep -q "check: passed" "$LOG/fwt-rec.log" && grep -q "pixels ok" "$LOG/fwt-rec.log" &&
		pass "fwt-rec: file checked by sei-dump (pixels ok)" ||
		fail "fwt-rec: check (see $LOG/fwt-rec.log)"
	"$ROOT/.venv/bin/python" -I -c '
import json, sys
d = json.load(open(sys.argv[1]))
assert d["recorded"]["status"] == "complete" and d["recorded"]["missing"] == 0
assert d["file"]["fetches"] >= 2 and d["file"]["frames"] == d["recorded"]["frames"]
assert d["check"]["result"] == "passed" and not d["plan"]["interrupted"]
' "$out.json" 2>"$LOG/fwt-rec-json.log" &&
		pass "fwt-rec: sidecar JSON valid, complete, $(grep -o '"fetches": [0-9]*' "$out.json")" ||
		fail "fwt-rec: sidecar JSON (see $LOG/fwt-rec-json.log)"
}

run_host() {
	local B="$ROOT/build-host/src" AP
	URL=http://127.0.0.1:18080
	mkdir -p "$LOG/rec"
	"$B/nodeagent/nodeagent" --port 18080 --rec "$LOG/rec" --trigd "$B/trigd/trigd" \
		--nodecap "$B/nodecap/nodecap" --trigd-args "--clock sys" \
		--nodecap-args "--enc x265 --width 960 --height 600" --ptp none --emmc none \
		--log-dir "$LOG" --trigd-sock "$LOG/t.sock" --ctl-sock "$LOG/rec.sock" \
		2>"$LOG/nodeagent.log" &
	AP=$!
	sleep 0.5
	scenario
	fwt_rec_run
	kill -INT $AP
	wait $AP && pass "nodeagent stopped cleanly" || fail "nodeagent exit (see $LOG/nodeagent.log)"
}

run_dev() {
	local BB="$ROOT/build-board/src"
	URL=http://${NODE#*@}:8080
	echo "== copy to $NODE:/tmp, start nodeagent"
	scp -q "$BB/trigd/trigd" "$BB/nodecap/nodecap" "$BB/nodeagent/nodeagent" "$NODE:/tmp/" &&
		pass "copied trigd, nodecap, nodeagent" || { fail "scp to $NODE"; return 1; }
	ssh -o BatchMode=yes "$NODE" 'pkill -INT -x nodeagent; sleep 0.5; \
		nohup /tmp/nodeagent >/tmp/nodeagent.log 2>&1 </dev/null & sleep 0.5; \
		pgrep -x nodeagent >/dev/null' && pass "nodeagent running on $NODE:8080" ||
		{ fail "nodeagent start (see $NODE:/tmp/nodeagent.log)"; return 1; }
	scenario
	ssh -o BatchMode=yes "$NODE" 'pkill -INT -x nodeagent; sleep 1; ! pgrep -x nodeagent' &&
		pass "nodeagent stopped cleanly" || fail "nodeagent stop on $NODE"
	scp -q "$NODE:/tmp/nodeagent.log" "$NODE:/tmp/nodecap.log" "$NODE:/tmp/trigd.log" "$LOG/" 2>/dev/null
}

case "$MODE" in
host)
	build_host && run_host
	;;
dev)
	[ -n "$NODE" ] || usage
	svc_pause "$NODE"
	build_host && build_dev && run_dev
	;;
*)
	usage
	;;
esac

finish "$MODE"
