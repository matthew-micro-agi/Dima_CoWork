#!/usr/bin/env bash
# End-to-end check of frame-to-edge binding by time (DL-26, DL-27):
# trigd + cam-emu + nodecap --src emu + sei-dump. The emulator draws the true
# edge index into every frame; sei-dump compares it with the SEI index nodecap
# found by timing alone, so a frame bound to the wrong edge can't go unnoticed.
#
#   tests/camemu-check.sh host               laptop: build, unit tests, x265 runs at
#                                            30 fps (x265 can't keep up with noisy
#                                            frames at 60 fps on the laptop)
#   tests/camemu-check.sh dev <user@node>    board:  cross-build, Wave5 runs at 60 fps,
#                                            1920x1200, PTP clock; streams checked here
#                                            e.g.    tests/camemu-check.sh dev root@10.42.0.132
#
# Runs: clean (every edge one frame), faults (drop/extra/late/sequence jump:
# counts must match the emulator's exactly, no wrong index), and on the laptop:
# window refusal, latency warning, emulator killed mid-session.
# Prints PASS/FAIL per step; exit code 0 only if every step passed.
# Nothing is installed on the board (binaries and output go to /tmp, a RAM disk).
set -u

MODE=${1:-}
NODE=${2:-}
NAME=camemu-check
. "$(dirname "$0")/check-lib.sh"

B="$ROOT/build-host/src"
SEI_DUMP="$B/tools/sei-dump/sei-dump"
EDGES=600
FAULTS="--drop 3 --extra 3 --late 3 --seq-jump 2 --seed 11"

usage() {
	echo "usage: $0 host | dev <user@node>" >&2
	exit 2
}

# line_field <file> <prefix> <field>: number after "<field> " in the last "<prefix>" line
line_field() {
	grep "^$2" "$1" | tail -1 | sed -n "s/.*[ ,(]$3 \([0-9-]*\).*/\1/p"
}

# expect_line <name> <file> <prefix> <field>=<value>...
expect_line() {
	local name=$1 file=$2 prefix=$3 got bad=""
	shift 3
	for kv in "$@"; do
		got=$(line_field "$file" "$prefix" "${kv%%=*}")
		[ "$got" = "${kv#*=}" ] || bad="$bad ${kv%%=*} ${got:-?} want ${kv#*=};"
	done
	[ -z "$bad" ] && pass "$name ($*)" || fail "$name:$bad see $file"
}

# Where and how runs happen: set by setup_host / setup_dev.
X=""         # directory with trigd, cam-emu, nodecap
W=""         # work directory for outputs (on the machine that runs them)
CLK=""       # clock option for trigd, cam-emu, nodecap
SIZE=""      # nodecap frame size
ENC=""       # nodecap encoder
FPS=""
RUN=""       # "local" or "ssh"
REC=""       # recorder directory of the clean run (board: on the eMMC, where syncs are slow)

setup_host() {
	X="$LOG/bin"
	W="$LOG"
	mkdir -p "$X"
	ln -sf "$B/trigd/trigd" "$B/tools/cam-emu/cam-emu" "$B/nodecap/nodecap" "$X/"
	CLK="--clock sys"
	SIZE="--width 960 --height 600"  # as nodecap-check: x265 at full size drops frames
	ENC="--enc x265"
	FPS=30
	RUN=local
	REC="$LOG/rec"
}

setup_dev() {
	local BB="$ROOT/build-board/src"

	X=/tmp
	W=/tmp/camemu-check
	CLK="--clock phc"
	SIZE="--width 1920 --height 1200"
	ENC="--enc v4l2"
	FPS=60
	RUN=ssh
	REC=/rec/check-camemu
	echo "== copy to $NODE:/tmp"
	scp -q "$BB/trigd/trigd" "$BB/tools/cam-emu/cam-emu" "$BB/nodecap/nodecap" "$NODE:/tmp/" &&
		ssh -o BatchMode=yes "$NODE" "rm -rf $W; mkdir -p $W" &&
		pass "copied trigd, cam-emu, nodecap" || { fail "scp to $NODE"; return 1; }
}

remote() {  # remote <shell command>: on the machine that runs the test
	if [ "$RUN" = ssh ]; then
		ssh -o BatchMode=yes "$NODE" "$1"
	else
		bash -c "$1"
	fi
}

# run_emu <name> <edges> "<cam-emu args>" "<nodecap args>": one run; afterwards
# $LOG/<name>.{emu,nc}.log and $LOG/<name>.h265 are here (copied back from the board).
run_emu() {
	local n=$1 edges=$2 eargs=$3 nargs=$4 P="$W/$1"
	local ES="$W/$1-emu.sock" TS="$W/$1-trigd.sock"

	remote "rm -f $P.h265 $ES $TS; \
		$X/cam-emu $CLK --sock $ES --trigd $TS --once $eargs >$P.emu.log 2>&1 & \
		$X/trigd --session $(new_session) $CLK --sock $TS --fps $FPS --count $edges \
			--t0-offset-ms 1500 >$P.trigd.log 2>&1 & \
		sleep 0.5; \
		timeout 60 $X/nodecap --src emu --cam-sock $ES $CLK --sock $TS $ENC $SIZE \
			--out $P.h265 -q $nargs >$P.nc.log 2>&1; \
		echo \"nodecap rc=\$?\" >>$P.nc.log; wait"
	if [ "$RUN" = ssh ]; then
		scp -q "$NODE:$P.emu.log" "$NODE:$P.nc.log" "$NODE:$P.trigd.log" "$LOG/" 2>/dev/null
		scp -q "$NODE:$P.h265" "$LOG/" 2>/dev/null
	fi
}

nc_rc() { sed -n 's/^nodecap rc=//p' "$LOG/$1.nc.log"; }

# emu_realtime <name>: the emulator delivered every frame less than a period
# after its timestamp (else it can't stand in for a camera at this rate)
emu_realtime() {
	local lag
	lag=$(sed -n 's/.*max delivery lag ms \([0-9.]*\).*/\1/p' "$LOG/$1.emu.log")
	if [ -n "$lag" ] && awk -v l="$lag" -v f="$FPS" 'BEGIN { exit !(l < 1000 / f) }'; then
		pass "$1 emulator kept real time (max delivery lag $lag ms < period)"
	else
		fail "$1 emulator too slow: max delivery lag ${lag:-?} ms (see $LOG/$1.emu.log)"
	fi
}

# check_stream <name> <pictures> <want rc>: sei-dump on the stream; the pixel
# check is the ground truth (drawn index = SEI index on every picture)
check_stream() {
	local out="$LOG/$1.sei-dump.log" rc
	"$SEI_DUMP" -q --frames "$2" "$LOG/$1.h265" >"$out" 2>&1
	rc=$?
	expect_fields "$1 stream" "$out" pictures="$2" duplicates=0 backward=0 no-sei=0
	grep -q "pixels ok" "$out" && pass "$1 stream: no frame bound to a wrong edge (drawn = SEI index)" ||
		fail "$1 stream: pixel check (see $out)"
	expect_rc "$1 sei-dump" $rc "$3"
}

# Clean run with the recorder on: storage stalls (eMMC syncs up to ~0.7 s)
# must never cost a camera frame (write queue, soak finding 2026-11-13).
run_clean() {
	echo "== clean: $EDGES edges, $FPS fps, no faults, recorder on ($REC)"
	remote "rm -rf $REC; mkdir -p $REC"
	run_emu clean $EDGES "" "--rec $REC --ring-max-mb 300 --ctl-sock $W/clean-rec.sock"
	expect_line "clean recorder" "$LOG/clean.nc.log" "recorder:" recorded=$EDGES errors=0
	expect_line "clean storage" "$LOG/clean.nc.log" "timing:" overflow=0
	grep "^timing:" "$LOG/clean.nc.log" | sed 's/^/      /'
	expect_line "clean emulator" "$LOG/clean.emu.log" "summary:" \
		edges=$EDGES delivered=$EDGES lost_nobuf=0 lost_send=0
	emu_realtime clean
	expect_fields "clean nodecap" "$LOG/clean.nc.log" \
		edges=$EDGES frames=$EDGES dropped=0 gaps=0 unmatched=0
	expect_line "clean camera" "$LOG/clean.nc.log" "camera:" \
		bound=$EDGES spurious=0 "edges without frame"=0 "sequence jumps"=0 "beyond half window"=0
	expect_rc "clean nodecap" "$(nc_rc clean)" 0
	check_stream clean $EDGES 0
}

# Faults: every count follows from the emulator's plan (window 3 ms rejects
# every wrong frame the emulator makes, DL-27):
#   edges without frame = dropped + late (a late frame lands 1/4 period off)
#   spurious (no edge)  = extra + late
run_faults() {
	echo "== faults: $EDGES edges, $FPS fps, $FAULTS"
	run_emu faults $EDGES "$FAULTS" ""
	local f="$LOG/faults.emu.log" d x l j
	d=$(line_field "$f" summary: dropped)
	x=$(line_field "$f" summary: extra)
	l=$(line_field "$f" summary: late)
	j=$(line_field "$f" summary: seq_jumps)
	if [ -z "$d" ] || [ $((d + x + l + j)) -eq 0 ]; then
		fail "faults emulator: no faults planned (see $f)"
		return
	fi
	echo "      emulator planned: dropped $d, extra $x, late $l, sequence jumps $j"
	expect_line "faults emulator" "$f" "summary:" edges=$EDGES lost_nobuf=0 lost_send=0
	emu_realtime faults
	expect_fields "faults nodecap" "$LOG/faults.nc.log" \
		edges=$EDGES frames=$((EDGES - d - l)) dropped=0 duplicates=0 unmatched=0
	expect_line "faults camera" "$LOG/faults.nc.log" "camera:" \
		bound=$((EDGES - d - l)) "edges without frame"=$((d + l)) "no edge"=$((x + l)) \
		"nearer frame won"=0 "sequence jumps"=$j
	expect_rc "faults nodecap (gaps)" "$(nc_rc faults)" 1
	check_stream faults $((EDGES - d - l)) 1
}

run_host_only() {
	local w=$((1000 / FPS / 4 + 1))  # ms: just above a quarter period

	echo "== window of a quarter period or more is refused"
	run_emu window 30 "" "--cam-window-ms $w"
	expect_rc "window ${w} ms at $FPS fps refused" "$(nc_rc window)" 1
	grep -q "below a quarter period" "$LOG/window.nc.log" &&
		pass "window refusal says why" || fail "window refusal message (see $LOG/window.nc.log)"

	echo "== timestamps spread +- 2 ms: bound, with a warning"
	run_emu jitter 150 "--jitter-ms 2" ""
	expect_line "jitter camera" "$LOG/jitter.nc.log" "camera:" bound=150 spurious=0
	grep -q "WARNING: frame for edge .* more than half the window" "$LOG/jitter.nc.log" &&
		pass "jitter: warning that the window is tight" ||
		fail "jitter: no warning (see $LOG/jitter.nc.log)"

	echo "== emulator killed mid-session"
	local ES="$W/kill-emu.sock" TS="$W/kill-trigd.sock" emu bound nof
	rm -f "$ES" "$TS"
	"$X/cam-emu" $CLK --sock "$ES" --trigd "$TS" >"$LOG/kill.emu.log" 2>&1 &
	emu=$!
	disown $emu  # killed on purpose: no job notice
	"$X/trigd" --session "$(new_session)" $CLK --sock "$TS" --fps $FPS --count 150 \
		--t0-offset-ms 1000 >"$LOG/kill.trigd.log" 2>&1 &
	sleep 0.3
	(sleep 3; kill -9 $emu) &
	timeout 30 "$X/nodecap" --src emu --cam-sock "$ES" $CLK --sock "$TS" $ENC $SIZE \
		--out "$LOG/kill.h265" -q >"$LOG/kill.nc.log" 2>&1
	echo "nodecap rc=$?" >>"$LOG/kill.nc.log"
	wait 2>/dev/null
	expect_rc "killed emulator: nodecap ends (124 = hung)" "$(nc_rc kill)" 1
	bound=$(line_field "$LOG/kill.nc.log" camera: bound)
	nof=$(line_field "$LOG/kill.nc.log" camera: "edges without frame")
	[ -n "$bound" ] && [ "$nof" -gt 0 ] 2>/dev/null && [ $((bound + nof)) -eq 150 ] &&
		pass "killed emulator: $bound bound, the remaining $nof edges are gaps" ||
		fail "killed emulator: bound ${bound:-?} + without frame ${nof:-?} != 150 (see $LOG/kill.nc.log)"
}

case "$MODE" in
host)
	build_host && setup_host && { run_clean; run_faults; run_host_only; }
	;;
dev)
	[ -n "$NODE" ] || usage
	svc_pause "$NODE"
	# sei-dump runs here, so the laptop build is needed too.
	build_host && build_dev && setup_dev && { run_clean; run_faults; }
	;;
*)
	usage
	;;
esac

finish "$MODE"
