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
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FAILS=0
LOG=$(mktemp -d /tmp/trigd-check.XXXXXX)

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; FAILS=$((FAILS + 1)); }

# summary_field <file> <field>: number after "<field> " in trig-listen's summary line
summary_field() {
	grep '^summary:' "$1" | tail -1 | sed -n "s/.*[ ,(]$2 \([0-9-]*\).*/\1/p"
}

# check_listen <name> <file> <edges> <gaps>: edges/gaps as expected, no duplicates
check_listen() {
	local e g d
	e=$(summary_field "$2" edges)
	g=$(summary_field "$2" gaps)
	d=$(summary_field "$2" duplicates)
	if [ "$e" = "$3" ] && [ "$g" = "$4" ] && [ "$d" = "0" ]; then
		pass "$1 (edges $e, gaps $g, duplicates $d)"
	else
		fail "$1 (edges ${e:-?} want $3, gaps ${g:-?} want $4, duplicates ${d:-?} want 0; see $2)"
	fi
}

build_host() {
	echo "== host build + unit tests"
	cmake -S "$ROOT" -B "$ROOT/build-host" >"$LOG/cmake-host.log" 2>&1 &&
		cmake --build "$ROOT/build-host" -j >>"$LOG/cmake-host.log" 2>&1 ||
		{ fail "host build (see $LOG/cmake-host.log)"; return 1; }
	if grep -q "warning:" "$LOG/cmake-host.log"; then
		fail "host build has warnings (see $LOG/cmake-host.log)"
	else
		pass "host build, no warnings"
	fi
	if ctest --test-dir "$ROOT/build-host" --output-on-failure >"$LOG/ctest.log" 2>&1; then
		pass "unit tests ($(grep -o '[0-9]*% tests passed.*' "$LOG/ctest.log" | head -1))"
	else
		fail "unit tests (see $LOG/ctest.log)"
	fi
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

build_dev() {
	echo "== board cross-build"
	cmake -S "$ROOT" -B "$ROOT/build-board" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/j722s-sdk.cmake" \
		>"$LOG/cmake-board.log" 2>&1 &&
		cmake --build "$ROOT/build-board" -j >>"$LOG/cmake-board.log" 2>&1 ||
		{ fail "board build (see $LOG/cmake-board.log)"; return 1; }
	grep -q "warning:" "$LOG/cmake-board.log" &&
		fail "board build has warnings (see $LOG/cmake-board.log)" ||
		pass "board build, no warnings"
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

echo
if [ $FAILS -eq 0 ]; then
	echo "ALL PASSED ($MODE). Logs: $LOG"
	exit 0
fi
echo "$FAILS FAILED ($MODE). Logs: $LOG"
exit 1
