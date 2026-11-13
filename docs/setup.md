# Setup: from a blank bench to a working node

How to reproduce our bench and results, step by step. The architecture (`architecture.md`) says what we build, the decision log (`decision-log.md`) says why; this document says **how to set it up**.

Each step has the exact commands, a **Verify** line and, where it changes something, an **Undo** line. Long commands are split with `\` so they paste as one command. Steps marked **NOT YET** are placeholders that grow with the work.

Names used below:
- **Laptop:** Ubuntu 24.04 host (`newuser@think`). Built-in Intel Ethernet port `enp0s31f6`.
- **Board:** TI J722S EVM (J722SXH01EVM), reachable as `root@10.42.0.132`.
- **SDK:** `~/ti/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05`.
- **Repo:** `~/Dima_CoWork`.

---

## 1. Bench hardware

| Item | Requirement | Pitfall |
|---|---|---|
| Power | **USB-C PD supply, 65 W or more**, with a USB-C to USB-C cable, into the EVM's power connector (J24) | A 5 V USB-A source (e.g. a monitor port, C-to-A cable) boots U-Boot but **brown-out resets** the board when Linux loads drivers |
| Console | micro-USB to the EVM's UART-over-USB connector (J6, FTDI FT4232H, four ports). J15 next to it is JTAG (XDS110), not the console | The Linux console is **if02** (`/dev/ttyS2` on the board); if00 is the R5 firmware console |
| Ethernet | cable from the EVM's RJ45 (J11) to the **laptop's built-in port** `enp0s31f6` | The laptop's USB Ethernet adapter has no usable hardware timestamping |
| Boot media | SD card with TI's Edge AI SDK ~11.0 image (as shipped) | Board boots from SD (SPL: "Trying to boot from MMC2") |
| Camera | none yet (the trial starts with a test source) | Never plug a camera with an unknown pinout into the 22-pin connectors (see `architecture.md` §2.2) |

**Verify:** after power-on, the Linux console reaches `j722s-evm login:` and the board stays up (`uptime` keeps counting).

---

## 2. Laptop (host)

### 2.1 Serial console

```sh
sudo usermod -aG dialout $USER      # then log out and in again
sudo apt install -y picocom tio
```

```sh
picocom -b 115200 \
  /dev/serial/by-id/usb-FTDI_USB__-__Serial_Converter_14251700474-if02-port0
```
Quit picocom with `ctrl-a` then `ctrl-x`. The FTDI serial number (`14251700474`) is specific to this EVM; list yours with `ls /dev/serial/by-id/`.

**Verify:** a root login prompt (no password) on if02.

### 2.2 TI SDK

Installer: `ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05-Linux-x86-Install.bin` (TI website, ~2.6 GB).
```sh
chmod +x ~/Downloads/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05-Linux-x86-Install.bin && \
mkdir -p ~/ti && \
~/Downloads/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05-Linux-x86-Install.bin \
  --mode unattended \
  --prefix ~/ti/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05
```
No `sudo` needed. About 8.9 GB.

**Verify:**
```sh
ls ~/ti/ti-processor-sdk-linux-adas-j722s-evm-11_02_02_05/linux-devkit/environment-setup-aarch64-oe-linux
```

Notes: user space matches the board image (glibc 2.39, GStreamer 1.22.12), so cross-compiled programs run as-is. The kernel does not match (SDK 6.12.57, board 6.12.17): kernel modules and kernel-dependent headers need care.

### 2.3 System packages

```sh
sudo apt update && \
sudo apt install -y \
  linuxptp python3-venv cmake pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev libmicrohttpd-dev \
  gstreamer1.0-plugins-bad \
  ffmpeg gdb-multiarch valgrind cppcheck clang-format
```
The dev packages are only for building and testing on the laptop; board binaries are always built against the SDK sysroot. The laptop has GStreamer 1.24, the board 1.22: avoid APIs newer than 1.22.

**Verify:** `command -v ptp4l phc2sys cmake ffprobe` and `gst-inspect-1.0 x265enc`, `gst-inspect-1.0 h265parse` (laptop encoder for `nodecap-check.sh host`).

### 2.4 Python tools (venv)

Ubuntu 24.04 blocks system-wide `pip`, so the tools live in a venv inside the repo (ignored by git).
```sh
cd ~/Dima_CoWork && \
python3 -m venv .venv && \
.venv/bin/pip install --upgrade pip && \
.venv/bin/pip install -r tools-requirements.txt
```
`tools-requirements.txt` pins `cbor2==5.9.0`: zcbor 0.9.1 fails to import with cbor2 6.x (decision log DL-03).

**Verify:**
```sh
~/Dima_CoWork/.venv/bin/zcbor --version && \
~/Dima_CoWork/.venv/bin/python -c \
  'import importlib.metadata as m; print("cbor2", m.version("cbor2"))'
```
Expected: `zcbor 0.9.1`, `cbor2 5.9.0`. (cbor2 5.x has no `__version__`.)

**Undo:** `rm -rf ~/Dima_CoWork/.venv`. Never copy a venv to another path; recreate it.

---

## 3. Network: laptop <-> board

A NetworkManager profile on the laptop's built-in port: the laptop gets `10.42.0.1`, hands the board an address by DHCP and shares its Wi-Fi internet (so the board gets NTP).
```sh
nmcli con add type ethernet ifname enp0s31f6 con-name board-link \
  ipv4.method shared ipv6.method link-local && \
nmcli con up board-link
```
On the board, if `eth0` has no address yet:
```sh
ip link set eth0 up; ip -br addr show eth0
```

**Verify:**
```sh
ping -c3 10.42.0.132 && \
ssh root@10.42.0.132 'uptime; timedatectl | grep synchronized'
```
Expected: replies, `System clock synchronized: yes`. SSH as root has no password on this image.

**Undo:** `nmcli con delete board-link`. After reflashing the SD card: `ssh-keygen -R 10.42.0.132`.

---

## 4. Board baseline

The SD card's TI Edge AI image is used **as shipped**. Expected state:

| What | Expected |
|---|---|
| `/run/media/BOOT-mmcblk1p1/uEnv.txt` | `dorprocboot=1`, `name_overlays=ti/k3-j722s-edgeai-apps.dtbo` |
| Remote cores | 2x C7x + main R5F attached (`dmesg \| grep remoteproc`); needed for the TI ISP path |
| Encoder | `/dev/video1` = `wave5-enc` |
| PTP clock | `/dev/ptp0`, `ethtool -T eth0` shows hardware TX/RX timestamping |
| Runtime tools | `gst-inspect-1.0 v4l2h265enc`, `ptp4l`, `phc2sys`, `mkfs.ext4`, `mmc`, `gdbserver` present |

**Verify (read-only):**
```sh
ssh root@10.42.0.132 \
  'grep -E "dorprocboot|name_overlays" /run/media/BOOT-mmcblk1p1/uEnv.txt; \
   v4l2-ctl -d /dev/video1 --info | grep "Card type"; \
   ethtool -T eth0 | grep -E "hardware-(transmit|receive)"; \
   for t in ptp4l phc2sys mkfs.ext4 mmc gdbserver; do command -v $t; done'
```

Idle extra services (containerd, pulseaudio, ofono, snmpd, strongswan, netserver, avahi, lldpd, mstpd, atd, rpcbind) run on this image. Stop them before measurement runs for repeatable numbers (see section 9).

---

## 5. Time: PTP bench setup

The laptop is grandmaster, the board is slave. **The laptop's Intel I219 hardware clock runs ~1600 ppm off**, so it only works as grandmaster with `phc2sys` disciplining it to the NTP-synced system clock (decision log DL-01, architecture gap G2).

Laptop, two terminals, leave both running:
```sh
# terminal 1: keep the laptop's hardware clock on its system clock (UTC)
sudo phc2sys -s CLOCK_REALTIME -c enp0s31f6 -O 0 -m
```
```sh
# terminal 2: PTP master
sudo ptp4l -i enp0s31f6 -2 -m \
  --tx_timestamp_timeout=50
```
Wait for `assuming the grand master role`.

Board, slave only:
```sh
ssh root@10.42.0.132 \
  'timeout 120 ptp4l -i eth0 -2 -s -m > /tmp/ptp4l.log 2>&1; \
   grep "master offset" /tmp/ptp4l.log | tail -10'
```

**Verify:** the board reaches `SLAVE` within seconds; `master offset` settles to tens of ns, `freq` around the board's crystal error (~-57 ppm), `path delay` positive and stable (~7.5 µs). Bad signs: `freq +488281` (pinned at the limit) and a negative path delay mean the laptop's `phc2sys` isn't running.

Notes:
- `ptp4l`'s frequency correction stays applied after it exits. Reset it with the test tool from section 9.2: `/tmp/testptp -d /dev/ptp0 -f 0`.
- This uses default 1588 settings, not yet the gPTP profile (`/usr/share/doc/linuxptp/configs/gPTP.cfg` on the laptop). With `-O 0` the PTP time is UTC, not TAI.

**Undo:** Ctrl-C both laptop terminals.

---

## 6. eMMC `/rec` partition

The whole eMMC becomes one partition for recordings (`architecture.md` §5.7). Done on the bench 2026-11-13.

**Check first (read-only):** root must be on the SD card (`mmcblk1`); tools present.
```sh
ssh root@10.42.0.132 \
  'lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS /dev/mmcblk0 /dev/mmcblk1; \
   echo "root: $(findmnt -n -o SOURCE /)"; \
   command -v sfdisk wipefs mkfs.ext4'
```

**Wipe, partition, format (destructive, irreversible):** one GPT partition over the whole device, ext4 labelled `rec`. Aborts if root is not on the SD card. The image automounts eMMC partitions under `/run/media/…`; they are unmounted first.
```sh
ssh root@10.42.0.132 \
  'set -e; \
   findmnt -n -o SOURCE / | grep -q mmcblk1; \
   for p in /dev/mmcblk0p*; do umount "$p" 2>/dev/null || true; done; \
   wipefs -a /dev/mmcblk0p* /dev/mmcblk0; \
   printf "label: gpt\n,,L\n" | sfdisk --wipe always /dev/mmcblk0; \
   sleep 1; \
   mkfs.ext4 -F -L rec \
     -E lazy_itable_init=0,lazy_journal_init=0 /dev/mmcblk0p1; \
   lsblk -o NAME,SIZE,FSTYPE,LABEL /dev/mmcblk0'
```
`lazy_*_init=0`: no background initialisation afterwards that would skew write measurements.

**Mount at `/rec`, permanently** (by label; `nofail` so a missing eMMC never blocks boot):
```sh
ssh root@10.42.0.132 \
  'mkdir -p /rec; \
   grep -q "^LABEL=rec " /etc/fstab || \
     echo "LABEL=rec /rec ext4 defaults,noatime,nofail 0 2" >> /etc/fstab; \
   umount /dev/mmcblk0p1 2>/dev/null; mount /rec && df -h /rec'
```
**Verify:** `lsblk` shows `mmcblk0p1 29.6G ext4 rec`; `df -h /rec` shows ~28 GB available, also after a reboot.

---

## 7. Device-tree overlay: CPTS timestamp loop (NOT YET)

Source: `dts/k3-j722s-evm-fwtrial-cpts-loop.dtso` (gap G1, decision log DL-09). Planned steps: compile with the SDK's `dtc`, copy to `/boot/dtb/ti/`, back up `uEnv.txt`, append to `name_overlays`, reboot, verify with `testptp` (timestamps on input 2 at the set rate). Undo: restore the `uEnv.txt` backup. To be written when deployed.

---

## 8. Build, test and run our code

Run from the repo root. Two builds of the same code (DL-05): laptop (unit tests, laptop runs) and board (cross-compiled against the SDK sysroot via `cmake/j722s-sdk.cmake`; set `FWT_SDK` if the SDK is not at the default path).

### 8.1 Build and unit tests
```sh
cmake -S . -B build-host && \
cmake --build build-host -j && \
ctest --test-dir build-host --output-on-failure
```
```sh
cmake -S . -B build-board -DCMAKE_TOOLCHAIN_FILE=cmake/j722s-sdk.cmake && \
cmake --build build-board -j
```
**Verify:** `100% tests passed`; no `warning:` in either build.

### 8.2 End-to-end checks
| Script | Mode | What it runs |
|---|---|---|
| `tests/trigd-check.sh` | `host` | build, unit tests; `trigd` on the system clock + `trig-listen`: 90 edges, gap detection, two subscribers |
| | `dev <user@node>` | cross-build, copy to `/tmp`; 300 edges on `/dev/ptp0`, stop with SIGINT, output disarmed |
| `tests/nodecap-check.sh` | `host` | build, unit tests; `trigd` + `nodecap` (x265, 960x600): 90 frames, gap detection; `sei-dump` on each stream |
| | `dev <user@node>` | laptop + board builds; 300 edges on `/dev/ptp0` with Wave5 on the board; stream copied back, `sei-dump` here |
| `tests/recorder-check.sh` | `host` | build, unit tests; `nodecap --rec` (x265) in temp dirs: record 300 + live lock + `rec-tool verify`; same session refused; `kill -9` + restart recovery; 4 MB ring wrap with a lock in an old session and an oversized lock refused |
| | `dev <user@node>` | cross-build, copy to `/tmp`; same scenario on the board with Wave5: main run into `/rec`, crash and ring runs in `/rec/check-crash`, `/rec/check-ring` (recreated each run) |
| `tests/nodeagent-check.sh` | `host` | build, unit tests; `nodeagent` on port 18080 (temp recordings, system clock, x265 960x600), driven with `curl` + `tools/icd-json`: identity, arm (+ busy), lock and health while recording, fetch while recording, disarm, sessions, segments, fetch last 5 s and whole session -> `sei-dump`, unlock, error replies |
| | `dev <user@node>` | laptop + board builds; `nodeagent` started on the board (port 8080, `/rec`, Wave5, PTP clock); same scenario over the network; `nodeagent` stopped at the end |

```sh
tests/trigd-check.sh host
tests/nodecap-check.sh dev root@10.42.0.132
```
Laptop runs use 960x600: the laptop's software encoder can't hold 30 fps at 1920x1200 and drops frames. The board runs full resolution.

**Verify:** each ends with `ALL PASSED (<mode>)`. Every step prints PASS/FAIL with the compared values; full logs in the `/tmp/<name>-check.XXXXXX` directory named at the end. Nothing is installed on the board (binaries and output in `/tmp`, a RAM disk).

### 8.3 Run by hand
The session number comes from the plan maker (DL-20); in the trial that is you or the script: pick a fresh one per run.
```sh
# board: 300 edges @30 fps on the PTP clock, Wave5, one stream + per-frame CSV in /tmp
S=$((RANDOM * 32768 + RANDOM + 1)); \
/tmp/trigd --session $S --count 300 2>/tmp/trigd.log & sleep 0.5; \
/tmp/nodecap --enc v4l2 --out /tmp/nc.h265 --log /tmp/nc.csv -q
```
```sh
# board: record into /rec (segments, manifest, ring), lock a range while recording
S=$((RANDOM * 32768 + RANDOM + 1)); \
/tmp/trigd --session $S --count 300 2>/dev/null & sleep 0.5; \
/tmp/nodecap --enc v4l2 --rec /rec -q & sleep 5; \
/tmp/rec-tool lock --session $S --from 60 --to 200 --ttl 600 --reason manual; \
wait
```
```sh
# board: inspect and check /rec
/tmp/rec-tool list -v      # sessions, segments (LOCKED / DELETED / recovered), locks
/tmp/rec-tool verify       # manifest vs files vs SEIs; exit code 0 = problems 0
/tmp/rec-tool unlock --id N
```
```sh
# laptop: check a stream (exit code 0 = pass)
build-host/src/tools/sei-dump/sei-dump -q --frames 300 nc.h265
```
- `trigd --help`, `nodecap --help`: options (clock, fps, T0 offset, count, encoder, bitrate, level, `--enc-extra` for extra Wave5 controls; recorder: `--rec`, `--ring-max-mb`, `--seg-seconds`, `--lock-cap-pct`, `--ctl-sock`).
- `rec-tool lock/unlock` go to the running recorder over `/run/fwtrial/rec.sock`; with no recorder running, `rec-tool` opens `/rec` itself.
- `nodecap` needs `trigd` running first; both stop at `trigd`'s END (`--count`) or Ctrl-C.

### 8.4 Control interface (nodeagent)

`nodeagent` is the node's only long-running process (DL-23): HTTP + CBOR per `icd/fwt-icd-v1.cddl`; it starts `trigd` + `nodecap` per armed session (children's logs in `/tmp`).
```sh
# board: start (defaults: port 8080, /rec, /tmp/trigd, /tmp/nodecap, Wave5, /dev/ptp0)
ssh root@10.42.0.132 \
  'nohup /tmp/nodeagent >/tmp/nodeagent.log 2>&1 </dev/null &'
```
```sh
# board: stop (an armed session is disarmed first)
ssh root@10.42.0.132 'pkill -INT -x nodeagent'
```
From the laptop: `curl` for HTTP, `tools/icd-json` for CBOR <-> JSON (laptop only, venv). Request keys in the ICD's order.
```sh
J="$HOME/Dima_CoWork/.venv/bin/python -I $HOME/Dima_CoWork/tools/icd-json"; \
U=http://10.42.0.132:8080/v1; \
curl -s $U/identity | $J to-json; \
curl -s $U/health | $J to-json
```
```sh
# arm session S at 30 fps (T0 chosen by the node: single node only), record, disarm
$J to-cbor '{"session": 12345, "fps": 30}' | \
  curl -s -X POST -H 'Content-Type: application/cbor' --data-binary @- $U/arm | $J to-json
curl -s -X POST $U/disarm | $J to-json
```
```sh
# list, lock, fetch (raw H.265; X-Fwt-* headers: actual range, missing ranges, open segment)
curl -s $U/sessions | $J to-json
curl -s "$U/segments?session=12345" | $J to-json
$J to-cbor '{"session": 12345, "from_idx": 0, "to_idx": 300, "ttl_s": 3600, "reason": "event"}' | \
  curl -s -X POST --data-binary @- $U/lock | $J to-json
curl -s -D - -o last10.h265 "$U/fetch?session=12345&last_s=10" | grep X-Fwt
build-host/src/tools/sei-dump/sei-dump -q last10.h265
```
- Laptop run: see `tests/nodeagent-check.sh` (`run_host`) for the options (`--trigd-args "--clock sys"`, `--nodecap-args "--enc x265 ..."`, `--ptp none`, temp sockets).
- After an ICD change: regenerate the C code (§8.5), then rebuild.

### 8.5 Regenerate the CBOR code (after an ICD change)

The node's CBOR encode/decode code is **generated** from the contract `icd/fwt-icd-v1.cddl` by zcbor (DL-03) and **committed** in `src/nodeagent/gen/`, together with zcbor's small C runtime. The board build compiles it into `nodeagent`: no CBOR library and no Python on the node. The generator runs on the laptop only.

| File in `src/nodeagent/gen/` | What |
|---|---|
| `icd_encode.c/.h`, `icd_decode.c/.h` | generated: one encode and one decode function per message (`cbor_encode_health`, `cbor_decode_arm_req`, ...) |
| `icd_types.h` | generated: one C struct per message |
| `zcbor_common/encode/decode/print.c/.h`, `zcbor_tags.h` | zcbor's C runtime, copied by the generator |

**When:** after any change to `icd/fwt-icd-v1.cddl`. Never edit the generated files by hand.

**Check the generator** (laptop venv, §2.4; zcbor version pinned in `tools-requirements.txt`):
```sh
cd ~/Dima_CoWork && \
.venv/bin/zcbor --version
```
Expected: `zcbor 0.9.1` (the generated files name the version in their first lines).

**Regenerate:**
```sh
cd ~/Dima_CoWork && \
src/nodeagent/gen.sh
```
The script runs exactly this (paths from the repo root):
```sh
cd src/nodeagent/gen && \
../../../.venv/bin/zcbor code \
  -c ../../../icd/fwt-icd-v1.cddl \
  -b 64 --default-max-qty 512 \
  -t error ok-rep identity health arm-req arm-rep session-rep \
     sessions-rep segments-rep lock-req lock-rep unlock-req \
  -d -e \
  --oc icd.c --oh icd.h --oht icd_types.h \
  --copy-sources
```
- `-c`: the contract. `-b 64`: 64-bit integers (ns times, indices). `--default-max-qty 512`: longest list = page size of sessions/segments.
- `-t`: the messages that get functions; **a new message type must be added here** (in `gen.sh`).
- `-d -e`: decode and encode (files get `_decode` / `_encode`). `--oht`: shared types. `--copy-sources`: copy the zcbor runtime.

**Review what changed:**
```sh
git diff --stat src/nodeagent/gen && \
git diff src/nodeagent/gen/icd_types.h
```
Expected: only the messages you changed; the runtime files unchanged (same zcbor version).

**Rebuild and check:**
```sh
cmake --build build-host -j && \
ctest --test-dir build-host --output-on-failure && \
tests/nodeagent-check.sh host
```
```sh
tests/nodeagent-check.sh dev root@10.42.0.132
```
Then adapt `src/nodeagent/main.c` to new or changed struct fields (the compiler points at them), and `tests/nodeagent-check.sh` to new endpoints.

**Commit:** the CDDL, `gen.sh` (if changed) and `src/nodeagent/gen/` together, with the regenerate command in the message.

**Rules from the generator:**
- Map keys are encoded and expected **in the CDDL order**: clients send request keys in that order (`tools/icd-json to-cbor` keeps the order you write).
- A message type name must not equal one of its own keys (`sessions = { "sessions" => ... }` fails: duplicate function name); hence `sessions-rep`.
- Additive changes only within ICD version 1 (new optional fields, new messages); anything else is a new `icd/fwt-icd-v2.cddl`.

Not yet: install paths, systemd units (DL-14), Yocto layer.

---

## 9. Pre-coding checks (rerun)

### 9.1 P-1 Encoder baseline

On the board (the encoder needs rate control, headers before every keyframe and explicit colorimetry, see `architecture.md` §3):
```sh
ssh root@10.42.0.132 \
  'gst-launch-1.0 -q videotestsrc num-buffers=60 pattern=snow \
     ! video/x-raw,format=NV12,width=1920,height=1200 \
     ! filesink location=/tmp/snow.nv12 && \
   time gst-launch-1.0 -q -e \
     multifilesrc location=/tmp/snow.nv12 loop=true \
     ! rawvideoparse format=nv12 width=1920 height=1200 framerate=30/1 colorimetry=bt709 \
     ! identity eos-after=600 \
     ! v4l2h265enc extra-controls="controls,frame_level_rate_control_enable=1,video_bitrate=5000000,video_bitrate_mode=1,video_gop_size=30,prepend_sps_and_pps_to_idr=1" \
     ! h265parse ! filesink location=/tmp/p1_30.h265; \
   ls -l /tmp/p1_30.h265'
```
**Verify:** ~600 frames in ~6 s (~100 fps). Then on the laptop:
```sh
scp root@10.42.0.132:/tmp/p1_30.h265 /tmp/ && \
ffprobe -v error -select_streams v -show_entries packet=flags -of csv=p=0 /tmp/p1_30.h265 | \
  grep -n K | head -3
```
Expected: keyframes at packets 1, 31, 61 (every 30 frames).

### 9.2 P-2 CPTS capabilities and periodic output

```sh
cd ~/Dima_CoWork && \
prereq/p2-cpts/build.sh && \
scp prereq/p2-cpts/build/testptp root@10.42.0.132:/tmp/testptp && \
ssh root@10.42.0.132 '/tmp/testptp -d /dev/ptp0 -c'
```
**Verify:** `4 external time stamp channels`, `2 programmable periodic signals`.

Timestamps of our own edges need the overlay (section 7). Without it, no events arrive on any input (expected on the current image).

### 9.3 P-3 PTP link

Section 5.

### 9.4 Before measurement runs

Stop the idle extra services (section 4) for repeatable CPU and bitrate numbers:
```sh
ssh root@10.42.0.132 \
  'systemctl stop containerd pulseaudio ofono snmpd strongswan-starter \
     netserver avahi-daemon lldpd mstpd atd rpcbind'
```
Not persistent: they come back after a reboot.
