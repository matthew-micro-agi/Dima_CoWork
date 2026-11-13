#!/bin/sh
# P-2 pre-coding check: build the kernel's PTP test tool (testptp) for the board.
#
# The source is NOT copied into this repo (GPL-2.0, part of the kernel); it is
# compiled straight from the TI SDK kernel tree with the SDK cross-compiler.
# Output: prereq/p2-cpts/build/testptp (ignored by git).
#
# Usage:  prereq/p2-cpts/build.sh
# Then:   scp prereq/p2-cpts/build/testptp root@10.42.0.132:/tmp/
set -eu

SDK=${SDK:-$HOME/ti/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05}
KSRC=$SDK/board-support/ti-linux-kernel-6.12.57+git-ti
SRC=$KSRC/tools/testing/selftests/ptp/testptp.c
OUT=$(dirname "$0")/build

[ -f "$SRC" ] || { echo "testptp.c not found: $SRC" >&2; exit 1; }
mkdir -p "$OUT/include/linux"

# The sysroot's libc headers predate testptp.c 6.12 (no PTP_MASK_* ioctls).
# Use the kernel's own UAPI ptp_clock.h; the board runs 6.12.17, which has them.
cp "$KSRC/include/uapi/linux/ptp_clock.h" "$OUT/include/linux/ptp_clock.h"

# SDK cross-compiler environment (sets CC with --sysroot etc.)
# shellcheck disable=SC1091
. "$SDK/linux-devkit/environment-setup-aarch64-oe-linux"

$CC -O2 -Wall -I"$OUT/include" -o "$OUT/testptp" "$SRC" -lrt
file "$OUT/testptp"
