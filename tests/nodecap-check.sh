#!/usr/bin/env bash
# End-to-end check of trigd + nodecap + sei-dump: every trigger edge becomes one
# encoded frame carrying its index (SEI and drawn into the picture).
#
#   tests/nodecap-check.sh host               laptop: build, unit tests, x265 runs
#   tests/nodecap-check.sh dev <user@node>    board:  cross-build, Wave5 run on the PTP
#                                             clock, stream copied back, checked here
#                                             e.g.    tests/nodecap-check.sh dev root@10.42.0.132
#
# Prints PASS/FAIL per step; exit code 0 only if every step passed.
# Nothing is installed on the board (binaries and output go to /tmp, a RAM disk).
set -u

MODE=${1:-}
NODE=${2:-}
NAME=nodecap-check
. "$(dirname "$0")/check-lib.sh"

B="$ROOT/build-host/src"
SEI_DUMP="$B/tools/sei-dump/sei-dump"
# Laptop runs at 960x600: x265 (software) at 1920x1200 can't hold 30 fps on the
# laptop and drops frames (measurement log, encoder entry). The board runs full
# resolution on Wave5.
HOST_SIZE="--width 960 --height 600"

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

# check_stream <name> <file.h265> <frames> <gaps> <want rc>: sei-dump result
check_stream() {
	local out="$LOG/$1.sei-dump.log" rc
	"$SEI_DUMP" -q --frames "$3" "$2" >"$out" 2>&1
	rc=$?
	expect_fields "$1 stream" "$out" pictures="$3" gaps="$4" duplicates=0 no-sei=0
	grep -q "pixels ok" "$out" && pass "$1 stream: drawn index = SEI index on every frame" ||
		fail "$1 stream: pixel check (see $out)"
	expect_rc "$1 sei-dump" $rc "$5"
}

run_host() {
	local T="$B/trigd/trigd" N="$B/nodecap/nodecap" S="$LOG/trigd.sock" rc

	echo "== host: 90 edges, system clock, x265 960x600"
	"$T" --session "$(new_session)" --clock sys --sock "$S" --fps 30 --count 90 --t0-offset-ms 800 2>"$LOG/h1-trigd.log" &
	sleep 0.3
	"$N" --enc x265 $HOST_SIZE --sock "$S" --out "$LOG/h1.h265" --log "$LOG/h1.csv" -q \
		>"$LOG/h1-nodecap.log" 2>&1
	rc=$?
	wait
	expect_fields "host nodecap" "$LOG/h1-nodecap.log" \
		edges=90 frames=90 dropped=0 gaps=0 unmatched=0
	expect_rc "host nodecap" $rc 0
	check_stream host "$LOG/h1.h265" 90 0 0

	echo "== host: gap detection (every 10th edge withheld)"
	"$T" --session "$(new_session)" --clock sys --sock "$S" --count 60 --t0-offset-ms 800 --test-skip 10 \
		2>"$LOG/h2-trigd.log" &
	sleep 0.3
	"$N" --enc x265 $HOST_SIZE --sock "$S" --out "$LOG/h2.h265" -q >"$LOG/h2-nodecap.log" 2>&1
	rc=$?
	wait
	# 6 edges withheld (9, 19, ..., 59); the last one is only visible via trigd's END.
	expect_fields "host gaps nodecap" "$LOG/h2-nodecap.log" edges=54 frames=54 gaps=6
	expect_rc "host gaps nodecap" $rc 1
	check_stream host-gaps "$LOG/h2.h265" 54 5 1
}

run_dev() {
	local BB="$ROOT/build-board/src" D="$LOG/dev" rc
	mkdir -p "$D"

	echo "== copy to $NODE:/tmp"
	scp -q "$BB/trigd/trigd" "$BB/nodecap/nodecap" "$NODE:/tmp/" &&
		pass "copied trigd, nodecap" || { fail "scp to $NODE"; return 1; }

	echo "== board: 300 edges on the PTP clock, Wave5"
	ssh -o BatchMode=yes "$NODE" \
		'rm -f /tmp/nc-check.h265 /tmp/nc-check.csv; \
		 /tmp/trigd --session '"$(new_session)"' --count 300 2>/tmp/nc-check-trigd.log & sleep 0.5; \
		 /tmp/nodecap --enc v4l2 --out /tmp/nc-check.h265 --log /tmp/nc-check.csv -q; \
		 echo "nodecap rc=$?"; wait; cat /tmp/nc-check-trigd.log' >"$D/d1.log" 2>&1
	expect_fields "board nodecap" "$D/d1.log" edges=300 frames=300 dropped=0 gaps=0 unmatched=0
	grep -q "nodecap rc=0" "$D/d1.log" && pass "board nodecap: exit code 0" ||
		fail "board nodecap: exit code (see $D/d1.log)"
	grep -q "clock /dev/ptp0" "$D/d1.log" && grep -q "output disarmed" "$D/d1.log" &&
		pass "board trigd: ran on /dev/ptp0, output disarmed" ||
		fail "board trigd: clock/disarm (see $D/d1.log)"

	echo "== copy the stream back, check it here"
	scp -q "$NODE:/tmp/nc-check.h265" "$NODE:/tmp/nc-check.csv" "$D/" &&
		pass "copied stream back" || { fail "scp from $NODE"; return 1; }
	check_stream board "$D/nc-check.h265" 300 0 0
	local lv
	lv=$(ffprobe -v error -select_streams v:0 -show_entries stream=level -of csv=p=0 \
		"$D/nc-check.h265")
	[ "$lv" = "150" ] && pass "board stream: H.265 level 5 signalled (150)" ||
		fail "board stream: level ${lv:-?}, want 150 (level 5)"
}

case "$MODE" in
host)
	build_host && run_host
	;;
dev)
	[ -n "$NODE" ] || usage
	# sei-dump runs here, so the laptop build is needed too.
	build_host && build_dev && run_dev
	;;
*)
	usage
	;;
esac

finish "$MODE"
