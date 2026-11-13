# Shared helpers for tests/*-check.sh (sourced, not run).
# Set NAME before sourcing; it names the log directory /tmp/NAME.XXXXXX.

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FAILS=0
LOG=$(mktemp -d "/tmp/${NAME:-check}.XXXXXX")

# new_session: a random session number (> 0). The laptop is the plan maker in
# the trial (DL-20): one number per run, passed to every node.
new_session() {
	echo $(( ((RANDOM << 16) | RANDOM) % 2147483647 + 1 ))
}

pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; FAILS=$((FAILS + 1)); }

# summary_field <file> <field>: number after "<field> " in the last summary line
summary_field() {
	grep '^summary:' "$1" | tail -1 | sed -n "s/.*[ ,(]$2 \([0-9-]*\).*/\1/p"
}

# expect_fields <name> <file> <field>=<value>...: every field has its value
expect_fields() {
	local name=$1 file=$2 got bad=""
	shift 2
	for kv in "$@"; do
		got=$(summary_field "$file" "${kv%%=*}")
		[ "$got" = "${kv#*=}" ] || bad="$bad ${kv%%=*} ${got:-?} want ${kv#*=};"
	done
	if [ -z "$bad" ]; then
		pass "$name ($*)"
	else
		fail "$name:$bad see $file"
	fi
}

# expect_rc <name> <rc> <want>
expect_rc() {
	[ "$2" -eq "$3" ] && pass "$1: exit code $2" || fail "$1: exit code $2, want $3"
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

# Installed node service (DL-24): board runs use the same port, sockets and
# /rec, so pause it for the run and start it again afterwards, also on failure.
SVC=fwtrial-nodeagent.service
SVC_NODE=""

svc_pause() {
	SVC_NODE=$1
	if ssh -o BatchMode=yes "$SVC_NODE" "systemctl is-active --quiet $SVC"; then
		ssh -o BatchMode=yes "$SVC_NODE" "systemctl stop $SVC" &&
			echo "== installed $SVC paused for this run" &&
			trap svc_resume EXIT
	else
		SVC_NODE=""
	fi
}

svc_resume() {
	[ -n "$SVC_NODE" ] || return 0
	ssh -o BatchMode=yes "$SVC_NODE" "systemctl start $SVC" &&
		echo "== installed $SVC started again"
	SVC_NODE=""
}

# finish <mode>: overall result and exit code
finish() {
	svc_resume
	echo
	if [ $FAILS -eq 0 ]; then
		echo "ALL PASSED ($1). Logs: $LOG"
		exit 0
	fi
	echo "$FAILS FAILED ($1). Logs: $LOG"
	exit 1
}
