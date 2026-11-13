#!/usr/bin/env bash
# End-to-end check of trigd + trig-listen.
#
#   tests/trigd-check.sh host               laptop: build, unit tests, system-clock runs
#   tests/trigd-check.sh dev <user@node>    board:  cross-build, copy to /tmp, PTP-clock runs
#                                           e.g.    tests/trigd-check.sh dev root@10.42.0.132
#
# Prints PASS/FAIL per step; exit code 0 only if every step passed.
# The board run arms the CPTS periodic output and checks it is disarmed again.
# Nothing is installed on the board (binaries go to /tmp, a RAM disk).
set -u

MODE=${1:-}
NODE=${2:-}
NAME=trigd-check
. "$(dirname "$0")/check-lib.sh"

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

# check_listen <name> <file> <edges> <gaps>: edges/gaps as expected, no duplicates
check_listen() {
	expect_fields "$1" "$2" edges="$3" gaps="$4" duplicates=0
}

run_host() {
	local B="$ROOT/build-host" S="$LOG/trigd.sock" T L
	T="$B/src/trigd/trigd"
	L="$B/src/tools/trig-listen/trig-listen"

	echo "== host: 90 edges, system clock, one listener"
	"$T" --clock sys --sock "$S" --fps 30 --count 90 --t0-offset-ms 500 2>"$LOG/h1-trigd.log" &
	sleep 0.3
	"$L" --sock "$S" -q >"$LOG/h1-listen.log" 2>&1
	wait
	check_listen "host 90 edges" "$LOG/h1-listen.log" 90 0

	echo "== host: gap detection (every 10th edge withheld)"
	"$T" --clock sys --sock "$S" --count 60 --t0-offset-ms 500 --test-skip 10 2>"$LOG/h2-trigd.log" &
	sleep 0.3
	"$L" --sock "$S" -q >"$LOG/h2-listen.log" 2>&1
	local rc=$?
	wait
	check_listen "host gap detection, 6 gaps expected" "$LOG/h2-listen.log" 54 6
	[ $rc -eq 1 ] && pass "host gap detection: listener exit code 1" ||
		fail "host gap detection: listener exit code $rc, want 1"

	echo "== host: two subscribers"
	"$T" --clock sys --sock "$S" --count 60 --t0-offset-ms 500 2>"$LOG/h3-trigd.log" &
	sleep 0.3
	"$L" --sock "$S" -q >"$LOG/h3-listen-a.log" 2>&1 &
	"$L" --sock "$S" -q >"$LOG/h3-listen-b.log" 2>&1 &
	wait
	check_listen "host subscriber A" "$LOG/h3-listen-a.log" 60 0
	check_listen "host subscriber B" "$LOG/h3-listen-b.log" 60 0
	grep -q "2 subscriber(s)" "$LOG/h3-trigd.log" && ! grep -q "dropped [1-9]" "$LOG/h3-trigd.log" &&
		pass "host: trigd saw 2 subscribers, no drops" ||
		fail "host: trigd subscribers/drops (see $LOG/h3-trigd.log)"
}

run_dev() {
	local B="$ROOT/build-board"

	echo "== copy to $NODE:/tmp"
	scp -q "$B/src/trigd/trigd" "$B/src/tools/trig-listen/trig-listen" "$NODE:/tmp/" &&
		pass "copied trigd, trig-listen" || { fail "scp to $NODE"; return 1; }

	echo "== board: 300 edges on the PTP clock, periodic output armed"
	ssh -o BatchMode=yes "$NODE" \
		'/tmp/trigd --count 300 2>/tmp/trigd-check-1.log & sleep 0.5; \
		 /tmp/trig-listen -q 2>&1; wait; cat /tmp/trigd-check-1.log' >"$LOG/d1.log" 2>&1
	check_listen "board 300 edges" "$LOG/d1.log" 300 0
	grep -q "clock /dev/ptp0" "$LOG/d1.log" && grep -q "output disarmed" "$LOG/d1.log" &&
		pass "board: ran on /dev/ptp0, output disarmed at the end" ||
		fail "board: clock/disarm (see $LOG/d1.log)"

	echo "== board: stop with SIGINT mid-session"
	ssh -o BatchMode=yes "$NODE" \
		'/tmp/trigd 2>/tmp/trigd-check-2.log & TP=$!; sleep 0.5; \
		 timeout 20 /tmp/trig-listen -q 2>&1 & LP=$!; sleep 4.5; \
		 kill -INT $TP; wait $TP; echo "trigd rc=$?"; wait $LP; cat /tmp/trigd-check-2.log' \
		>"$LOG/d2.log" 2>&1
	local e
	e=$(summary_field "$LOG/d2.log" edges)
	if [ "$(summary_field "$LOG/d2.log" gaps)" = "0" ] && [ "${e:-0}" -gt 0 ]; then
		pass "board SIGINT: listener got $e edges, gaps 0"
	else
		fail "board SIGINT: listener (see $LOG/d2.log)"
	fi
	grep -q "trigd rc=0" "$LOG/d2.log" && grep -q "output disarmed" "$LOG/d2.log" &&
		pass "board SIGINT: trigd exit 0, output disarmed" ||
		fail "board SIGINT: exit/disarm (see $LOG/d2.log)"
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
