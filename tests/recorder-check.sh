#!/usr/bin/env bash
# End-to-end check of the recorder (inside nodecap) + rec-tool:
#   record + live lock + verify, session reuse refused, kill -9 + recovery,
#   ring wrap with a lock in an old session and a refused oversized lock.
#
#   tests/recorder-check.sh host               laptop: x265 at 960x600, temp directories
#   tests/recorder-check.sh dev <user@node>    board:  Wave5; main run into /rec, crash and
#                                              ring runs in /rec/check-* (recreated each run)
#                                              e.g.    tests/recorder-check.sh dev root@10.42.0.132
#
# Prints PASS/FAIL per step; exit code 0 only if every step passed.
set -u

MODE=${1:-}
NODE=${2:-}
NAME=recorder-check
. "$(dirname "$0")/check-lib.sh"

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

# The scenario runs as one shell script, on the laptop (host) or the board (dev).
# Parameters: $1 = bin dir, $2 = main rec dir, $3 = scratch dir, $4 = encoder args,
#             $5 = trigd clock args, $6.. = session numbers (main, crash, crash-restart,
#             ring-1, ring-2)
scenario() {
	cat <<'EOF'
set -u
BIN=$1; REC=$2; TMP=$3; ENC=$4; CLK=$5; S1=$6; S3=$7; S4=$8; S5=$9; S6=${10}
T=$BIN/trigd; N=$BIN/nodecap; R=$BIN/rec-tool
TS=$TMP/trigd.sock; CS=$TMP/rec.sock
mkdir -p "$REC" "$TMP"
nc() { "$N" $ENC --sock "$TS" --ctl-sock "$CS" -q "$@"; }
tg() { "$T" $CLK --sock "$TS" --t0-offset-ms 800 "$@" 2>/dev/null; }

echo "== 1 record 300 + live lock"
tg --session $S1 --count 300 & sleep 0.5
nc --rec "$REC" >"$TMP/1.out" 2>&1 & NP=$!
sleep 5
"$R" --dir "$REC" --sock "$CS" lock --session $S1 --from 60 --to 400 --ttl 3600 --reason check
wait $NP; echo "nodecap rc=$?"; wait
grep -E "^(summary|recorder: recorded)" "$TMP/1.out"
"$R" --dir "$REC" list -v | grep -A3 "^$S1 "
"$R" --dir "$REC" verify | tail -1; echo "verify rc=$?"

echo "== 2 same session again"
tg --session $S1 --count 30 & sleep 0.5
nc --rec "$REC" 2>&1 | grep -c "refused" | sed 's/^/refused lines /'; wait

echo "== 3 kill -9 mid-session, restart recovers"
C=$REC/check-crash; rm -rf "$C"; mkdir -p "$C"
# binaries started directly: $! must be the process itself, not a shell wrapper
"$T" $CLK --sock "$TS" --t0-offset-ms 800 --session $S3 --count 600 2>/dev/null & TP=$!
sleep 0.5
"$N" $ENC --sock "$TS" --ctl-sock "$CS" -q --rec "$C" >/dev/null 2>&1 & NP=$!
sleep 6.5; kill -9 $NP; kill -INT $TP; wait 2>/dev/null
tg --session $S4 --count 30 & sleep 0.5
nc --rec "$C" 2>&1 | grep -E "recovery|^summary"; wait
"$R" --dir "$C" list | grep "^$S3 "
"$R" --dir "$C" verify | tail -1; echo "crash verify rc=$?"

echo "== 4 ring 4 MB: closed session locked part survives, too-big lock refused"
G=$REC/check-ring; rm -rf "$G"; mkdir -p "$G"
RING="--rec $G --ring-max-mb 4 --lock-cap-pct 50 --bitrate 2000000"
tg --session $S5 --count 240 & sleep 0.5; nc $RING >/dev/null 2>&1; wait
tg --session $S6 --count 900 & sleep 0.5; nc $RING >"$TMP/4.out" 2>&1 & NP=$!
sleep 4
"$R" --dir "$G" --sock "$CS" lock --session $S5 --from 0 --to 50 --ttl 600 --reason keep
"$R" --dir "$G" --sock "$CS" lock --session $S6 --from 0 --to 899 --ttl 600 --reason too-big
wait $NP; wait
grep -E "^recorder: recorded" "$TMP/4.out"
"$R" --dir "$G" list -v | grep -v DELETED
echo "ring used KB $(du -sk "$G" | cut -f1)"
"$R" --dir "$G" verify | tail -1; echo "ring verify rc=$?"
EOF
}

# Checks on the scenario output ($1).
check_output() {
	local o=$1
	grep -q "^nodecap rc=0" "$o" && pass "record: nodecap exit code 0" ||
		fail "record: nodecap exit code (see $o)"
	grep -q "recorded 300, skipped 0, errors 0, segments closed 3" "$o" &&
		pass "record: 300 frames in 3 segments, no errors" ||
		fail "record: frames/segments (see $o)"
	grep -q "lock ok: locked" "$o" && grep -q "LOCKED" "$o" &&
		pass "record: live lock granted via control socket, segments LOCKED" ||
		fail "record: live lock (see $o)"
	grep -q "^verify rc=0" "$o" && pass "record: rec-tool verify, no problems" ||
		fail "record: verify (see $o)"
	grep -q "^refused lines [1-9]" "$o" && pass "same session again: refused (DL-20)" ||
		fail "same session again not refused (see $o)"
	grep -q "recovery: session [0-9]* marked interrupted" "$o" &&
		grep -q "interrupted" "$o" &&
		pass "kill -9: restart recovered the open segment, session interrupted" ||
		fail "kill -9 recovery (see $o)"
	grep -q "^crash verify rc=0" "$o" && pass "kill -9: verify after recovery, no problems" ||
		fail "kill -9: verify (see $o)"
	grep -q "lock ok: locked (lock id [0-9]*, via running recorder)" "$o" &&
		grep -q "lock FAILED: lock cap" "$o" &&
		pass "ring: lock in closed session granted, oversized lock refused (cap)" ||
		fail "ring: locks (see $o)"
	grep -q "truncated by ring" "$o" && pass "ring: wrapped (sessions truncated by ring)" ||
		fail "ring: did not wrap (see $o)"
	sed -n '/== 4/,$p' "$o" | grep -q "recorded 900, skipped 0, errors 0" &&
		pass "ring: 900 frames recorded while wrapping, no errors" ||
		fail "ring: frames recorded while wrapping (see $o)"
	sed -n '/== 4/,$p' "$o" | grep -q "LOCKED" &&
		pass "ring: locked segment of the closed session survived the wrap" ||
		fail "ring: locked segment did not survive (see $o)"
	local kb
	kb=$(sed -n 's/^ring used KB //p' "$o")
	[ -n "$kb" ] && [ "$kb" -le 4200 ] && pass "ring: disk use ${kb} KB <= 4 MB limit" ||
		fail "ring: disk use ${kb:-?} KB (see $o)"
	grep -q "^ring verify rc=0" "$o" && pass "ring: verify, no problems" ||
		fail "ring: verify (see $o)"
}

sessions() {
	echo "$(new_session) $(new_session) $(new_session) $(new_session) $(new_session)"
}

run_host() {
	local B="$ROOT/build-host/src" BIN="$LOG/bin"
	mkdir -p "$BIN"
	ln -sf "$B/trigd/trigd" "$B/nodecap/nodecap" "$B/tools/rec-tool/rec-tool" "$BIN/"
	echo "== host scenario (x265, 960x600)"
	# shellcheck disable=SC2046
	bash -c "$(scenario)" scenario "$BIN" "$LOG/rec/main" "$LOG/tmp" \
		"--enc x265 --width 960 --height 600" "--clock sys" $(sessions) \
		>"$LOG/host.log" 2>&1
	check_output "$LOG/host.log"
}

run_dev() {
	local BB="$ROOT/build-board/src"
	echo "== copy to $NODE:/tmp"
	scp -q "$BB/trigd/trigd" "$BB/nodecap/nodecap" "$BB/tools/rec-tool/rec-tool" "$NODE:/tmp/" &&
		pass "copied trigd, nodecap, rec-tool" || { fail "scp to $NODE"; return 1; }
	echo "== board scenario (Wave5, /rec)"
	scenario >"$LOG/scenario.sh"
	scp -q "$LOG/scenario.sh" "$NODE:/tmp/recorder-scenario.sh" ||
		{ fail "scp scenario"; return 1; }
	# shellcheck disable=SC2046
	ssh -o BatchMode=yes "$NODE" bash /tmp/recorder-scenario.sh /tmp /rec /tmp/rc-check \
		"'--enc v4l2'" "''" $(sessions) >"$LOG/dev.log" 2>&1
	check_output "$LOG/dev.log"
}

case "$MODE" in
host)
	build_host && run_host
	;;
dev)
	[ -n "$NODE" ] || usage
	build_dev && run_dev
	;;
*)
	usage
	;;
esac

finish "$MODE"
