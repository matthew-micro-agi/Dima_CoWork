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

## 6. eMMC `/rec` partition (NOT YET)

Planned (`architecture.md` §5.7): wipe `mmcblk0p2`, format as ext4 labelled `rec`, mount at `/rec`, add an `fstab` entry. Destructive; to be written when done.

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
| `tests/nodecap-check.sh` | `host` | build, unit tests; `trigd` + `nodecap` (x265): 90 frames, gap detection; `sei-dump` on each stream |
| | `dev <user@node>` | laptop + board builds; 300 edges on `/dev/ptp0` with Wave5 on the board; stream copied back, `sei-dump` here |

```sh
tests/trigd-check.sh host
tests/nodecap-check.sh dev root@10.42.0.132
```
**Verify:** each ends with `ALL PASSED (<mode>)`. Every step prints PASS/FAIL with the compared values; full logs in the `/tmp/<name>-check.XXXXXX` directory named at the end. Nothing is installed on the board (binaries and output in `/tmp`, a RAM disk).

### 8.3 Run by hand
```sh
# board: 300 edges @30 fps on the PTP clock, Wave5, stream + per-frame CSV in /tmp
/tmp/trigd --count 300 2>/tmp/trigd.log & sleep 0.5; \
/tmp/nodecap --enc v4l2 --out /tmp/nc.h265 --log /tmp/nc.csv -q
```
```sh
# laptop: check a stream (exit code 0 = pass)
build-host/src/tools/sei-dump/sei-dump -q --frames 300 nc.h265
```
- `trigd --help`, `nodecap --help`: options (clock, fps, T0 offset, count, encoder, bitrate, `--enc-extra` for extra Wave5 controls).
- `nodecap` needs `trigd` running first; both stop at `trigd`'s END (`--count`) or Ctrl-C.

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
