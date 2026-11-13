# Firmware Architecture: J722S Recording Node

Status: **DRAFT v0.3**, 2026-11-12.
Marks: *(verify)* = not yet checked on the bench; **TBD** = decision still open.

This document describes **our node as implemented on the TI J722S EVM**: our camera stack, processes, devices and pins. The EVM is a stand-in (Architecture Context, AC §3). Every component is therefore tagged with what changes when we move:

| Tag | Meaning | Changes when... |
|---|---|---|
| **[EVM]** | Specific to the J722S EVM board | we move to our own carrier board |
| **[TI]** | Specific to the TI J722S/AM6x SoC family and its SDK | we move to another SoC family (e.g. Rockchip) |
| **[OURS]** | Our own portable code and formats | never; carried to every node |

Interchangeability with other node implementations is defined in §8 as a set of requirements, not as shared code.

Why each choice was made, the alternatives and dead ends: `decision-log.md` (DL-xx references below).

---

## 1. Summary

**TBD.** Write last: what runs, what was measured, what is stubbed.

### 1.1 Time and data model: answers to the brief

| Question (brief 5.1) | Answer | Detail | Shown on bench |
|---|---|---|---|
| Camera 7 frame 41200 = camera 2 frame 41200? | The controller sends one plan `{session, T0, period}` to all nodes. Every node fires hardware edges at `T0 + k·period` on PTP time and defines `index = round((t_edge − T0)/period)`. Same (session, index) = same edge = same instant. Each frame carries its measured edge time; the difference between cameras is the measured sync error. | §4 | One node: edge grid (M4), PTP lock (M5). Multi-node: method only (§10.2). |
| Dropped frame detected, not absorbed? | Index computed from time, never counted. Frames matched to edges by time. Edge without frame = gap (recorded, index never reused); frame without edge = spurious. With a camera, the sensor frame counter is a third check. | §4.4 | M7, with drops injected on purpose |
| "Last 90 s" is cheap? | Manifest (on eMMC, mirrored in RAM) with one record per 4 s segment, including keyframe offsets. Binary search in RAM, then read only the needed bytes. No eMMC scan. | §5.3, §5.5 | M9 |
| Event lock vs a reclaiming ring? | Locks cover whole segments, with reason and TTL, and can reach into the future (post-event window). Reclaim never deletes locked segments. A lock cap keeps the ring able to free space; a lock over the cap is refused and reported. | §5.4 | M8 |

### 1.2 Bench facts (day 1)

| Item | Finding |
|---|---|
| Board | J722S EVM (J722SXH01EVM), SoC J722S SR1.0 HS-FS |
| Image on SD | TI Edge AI SDK ~11.0: Arago 2025.01, kernel 6.12.17-ti, U-Boot 2025.01, SYSFW 11.0.9 |
| Host SDK | Processor SDK Linux ADAS 11.02.02.05: kernel 6.12.57, glibc 2.39, GStreamer 1.22.12 |
| User space | Board matches the SDK (glibc 2.39, GStreamer 1.22.12): cross-compiled apps run as-is |
| Kernel | Board 6.12.17 vs SDK 6.12.57: drivers/overlays need a matching kernel or a new image |
| Encoder | Wave5 V4L2 M2M: /dev/video1 = encoder, /dev/video0 = decoder |
| Remote cores | 2x C7x + main R5F attached (IPC-only), needed for the TI ISP path |
| PTP | CPSW eth0: hardware TX/RX timestamping, /dev/ptp0 |
| Network | Direct link to host, 10.42.0.0/24 with NAT, NTP synced |
| Camera | **Blocked:** module believed AR0234 (maybe SC233x), 20-pin cable vs the EVM's 22-pin connectors, no driver in the TI SDK |
| Power | USB-C PD 65 W+ required; a 5 V USB-A source brown-outs during driver load |

---

## 2. The node: J722S EVM

### 2.1 SoC blocks we use

| Block | Use | Linux interface | Tag |
|---|---|---|---|
| 4x Cortex-A53 | Linux: all our processes | - | [TI] |
| CSI2RX0..3 (Cadence + TI wrapper) | Sensor input, 4 lanes each | V4L2 / media controller | [TI] |
| VPAC (ISP: VISS, LDC, MSC) | Bayer -> NV12 | GStreamer `tiovx*` elements; runs on R5F/C7x firmware | [TI] |
| Main R5F, 2x C7x | Remote firmware for TIOVX (ISP control); used as shipped | remoteproc / RPMsg, IPC-only | [TI] |
| Wave5 | H.265 encode | V4L2 M2M (/dev/video1), GStreamer `v4l2h265enc` | [TI] |
| CPSW3G + CPTS | Ethernet, PTP clock; 2 periodic outputs (GENF), 4 timestamp inputs (HW_TS_PUSH) | eth0, /dev/ptp0 | [TI] |
| eMMC 29.6 GB, HS200 (sdhci0) | Segment ring | /dev/mmcblk0 | [EVM] (size) |
| SD card (sdhci1) | Boot + rootfs on the bench | /dev/mmcblk1 | [EVM] |

Not used: GPU, display, C7x deep learning, PCIe, USB3, CAN.

Ours vs TI's: all firmware (R5F/C7x, Wave5, SYSFW), U-Boot and the TI kernel are used as shipped. Ours are the Linux user space, device-tree overlays, the sensor driver and ISP tuning data (AC §7).

### 2.2 Camera connectors [EVM]

From TI SPRUJB5 (Fig. 2-1/2-2, Tables 2-6, 2-19):

| Board label | Connector | Side | CSI | I2C switch / ch | Pin 17 (CAM_PWDN) | Pin 18 (CAM_AUX) |
|---|---|---|---|---|---|---|
| Camera 1 | J33 | bottom | CSI2RX0 | 0x70 / 0 | GPMC0_AD0 / GPIO0_15 | GPMC0_AD1 / GPIO0_16 |
| Camera 2 | J34 | bottom | CSI2RX1 | 0x70 / 1 | GPMC0_AD2 / GPIO0_17 | GPMC0_AD3 / GPIO0_18 |
| Camera 3 | J2 | top | CSI2RX2 | 0x71 / 0 | GPMC0_AD4 / GPIO0_19 | GPMC0_AD5 / GPIO0_20 |
| Camera 4 | J1 | top | CSI2RX3 | 0x71 / 1 | GPMC0_AD6 / GPIO0_21 | GPMC0_AD7 / GPIO0_22 |

- 22-pin pinout: 4 MIPI lanes + clock, I2C, pins 17/18, **3.3 V only** (pin 22), no MCLK. The I/O level is 1.8 or 3.3 V, set with SW2.4.
- Pins 17/18 go straight to SoC pads, not through the I2C expander, so a timed edge is possible.
- Default routing goes to the 40-pin J36/J37. A camera overlay sets `CSI01_MUX_SEL_2` / `CSI23_MUX_SEL_2` high and releases the sensor reset.
- **Our choice: Camera 4 (J1)**, top side. Pin 17 = sensor reset (as in TI's IMX219 overlay); **pin 18 = trigger line**.

### 2.3 Sensor

- Product sensor: onsemi AR0235. Bench module: AR0234 (same family), **not detected on any EVM connector** and no driver in the image: gap G3 (§11.6).
- **Camera choice rule for the trial:** a camera with an existing driver in the TI SDK, preferably one whose sensor and driver support an external trigger. Without trigger support, the trigger chain is proven to the connector pin with the test source, and the gap is stated.
- **The AR0235 driver and ISP tuning are ours** (AC §7, §9) and a later milestone: V4L2 subdev with trigger (slave) mode, manual exposure/gain/white balance, frame counter. The overlay is based on `k3-j722s-evm-csi2-quad-rpi-cam-imx219.dtso`.

---

## 3. Data path

```
frame source:  test (one frame per edge, built in)
             | emu  (cam-emu process, V4L2-like buffers; binding by time)
             | AR023x -> CSI2RX3 -> V4L2 -> ISP (camera, later; same binding as emu)
      -> appsrc -> NV12 1920x1200 -> v4l2h265enc (Wave5) -> h265parse -> appsink
      -> our code: SEI {session, index, t} + drop check -> recorder -> eMMC
```

**Frame sources** (`nodecap --src`, DL-26), one interface shaped like a V4L2 capture device (fd to poll, dequeue a buffer with monotonic timestamp and sequence counter, release it):
- `test`: frames made from edges, bound by construction; kept as a second path.
- `emu`: `cam-emu`, a separate test process (not in the bundle) playing a triggered sensor: subscribes to `trigd`, delivers each frame at edge + latency ± jitter into shared-memory buffers the client queued (one memfd, passed over a Unix socket); no buffer queued -> frame lost, sequence still advances. Faults on request; the true index drawn into each frame (ground truth).
- Later `v4l2`: the camera; same calls as `ioctl`s.

**GStreamer** is the media framework [TI elements, standard framework] (DL-08):
- **Now:** a thin pipeline; our code sits at `appsrc` (one frame per trigger edge) and `appsink`.
- **Later:** the same logic wrapped as our own elements (`trigsrc`, `trigsei`, `segmentsink`), so the whole node is one portable pipeline. On Rockchip only the middle changes (`mpph265enc`).
- Logic is written as plain functions (SEI building, drop check, segment/manifest writing) so it can be wrapped without rewriting.
- Each frame's buffer timestamp is **PTS = edge time − T0** (DL-15), carried through the encoder. Measured: P-1 saw Wave5 lose one and repeat one timestamp at stream start; `nodecap` (300 edges) saw none. Either way it is **checked at the encoder output** (§4.4), never trusted blindly.

**Mandatory encoder settings** (measured in P-1, `v4l2h265enc extra-controls`):

| Control | Value | Why |
|---|---|---|
| `frame_level_rate_control_enable` | 1 | default 0 = rate control off: bitrate is ignored (fixed QP) |
| `video_bitrate`, `video_bitrate_mode` | per AC §2, CBR | bitrate target |
| `video_gop_size` | = fps | keyframe every 1 s |
| `prepend_sps_and_pps_to_idr` | 1 | default 0; headers before every keyframe, so every 4 s segment decodes on its own (verified) |
| level | 5, via caps `video/x-h265,level=(string)5` after the encoder | 1920x1200 exceeds level 4.1. The `hevc_level` control is overridden by caps negotiation (without a caps level the stream signals an invalid "10"); verified: stream signals 150 = level 5 |
| input caps | `colorimetry=bt709`, `interlace-mode=progressive`, width/height multiples of 8 | the encoder only accepts caps with these fields set explicitly (a missing field = not-negotiated) |

- **End of stream:** Wave5 does not forward EOS; `nodecap` treats "every pushed frame came out" as drained.
- **Laptop encoder** (`x265enc`, tests only): needs I420 (`videoconvert`) and shifts output PTS by 1000 h, recorded in the segment, so `nodecap` reads PTS as stream time, never raw.

Measured capacity: **~100 fps at 1920x1200** (need 60). Bitrate on noise (worst case) ~2x the target; real content to be measured with a camera. The test source pushes pre-made buffers: rendering per frame (`videotestsrc`) costs a full core at 10-40 fps.

| Stage | Format | Rate (1920x1200) |
|---|---|---|
| CSI-2 | RAW10 | ~2.3 MB/frame -> 69 MB/s @30, 138 MB/s @60 |
| ISP out | NV12 | ~3.5 MB/frame -> 104 MB/s @30, 207 MB/s @60 (in DDR) |
| Encoder out | H.265 Main 8-bit 4:2:0 | ~5 Mbps @30, 8-10 Mbps @60 (AC §2) |
| eMMC write | segments | 0.6-1.2 MB/s sustained |

Keyframe (IDR) every 1 s (**TBD**: 1 or 2 s); every segment starts with a keyframe.

---

## 4. Timing

### 4.1 Trigger generation in hardware [TI + EVM]

- `trigd` (C) makes one request: a CPTS periodic output (GENF) starting at **T0**, period `1/fps` (standard Linux PTP `PTP_PEROUT_REQUEST`). **The CPTS fires every edge in hardware** on PTP time. Linux scheduling cannot delay or shift an edge.
- Each edge is **timestamped in hardware** on a CPTS input (HW_TS_PUSH), routed from GENF1 to HW3_TS_PUSH through the timesync router (our overlay, G1 closed, `setup.md` §7). `trigd` reads them from the kernel's event queue (`PTP_EXTTS_REQUEST`). A late read delays *when* we learn an edge, never *what time* it carries. `trigd --edges auto` (default) uses them when present, else computed times with a WARNING; an edge whose event is missing is still announced, flagged `TS_MISSING` (DL-09).
  - **Measured (board, 60 and 30 fps):** every edge captured, a constant +14 ns from T0 + k·P (routing delay); every interval = the period (0 ns at the 2 ns resolution).
- The CPTS runs at 500 MHz: 2 ns timestamp resolution, and it makes periods in **whole 2 ns ticks**. Periods are therefore chosen as whole ticks (30 fps = 33,333,334 ns): an odd period ran 1 ns short per edge and the edges drifted off the plan (30 ppb, seen on the board). A periodic output keeps running after its file descriptor closes, so `trigd` disables it explicitly on stop.
- When `ptp4l` adjusts the clock, the CPTS adjusts the edge frequency in hardware: edges stay locked to network time. *(To verify with measured edges while PTP is locked: M4 under PTP.)*
- **Route to the sensor:** GENF -> pin mux (our overlay) -> SoC pad GPMC0_AD7 -> J1 pin 18 -> module -> sensor trigger input.
  - **Open:** can GENF (or a timer output) be muxed onto GPMC0_AD7? *(verify in the J722S pin mux table.)* If yes, it's an overlay only. If only a DMTimer output is available, discipline it from PTP via the timesync router. If neither, a jumper wire from a GENF-capable pad (ask Matthew).
  - The I/O level (SW2.4) must match the module.

### 4.2 Time base

- PHC (/dev/ptp0) is the reference; `ptp4l` (gPTP profile) on eth0, `phc2sys` keeps CLOCK_REALTIME aligned. Why PTP, alternatives and requirements at scale: DL-01.
- Bench: laptop Intel port (enp0s31f6) as grandmaster, **disciplined by `phc2sys` to its NTP-synced system clock** (its hardware clock runs ~1600 ppm off on its own). Product: the controller's TSN switch (AC §8).
- **Measured (P-3):** board as slave over the direct cable, **30 ns RMS / 85 ns max offset**, board crystal ~57 ppm, path delay +7.5 µs (stable). Absolute accuracy is not measured (gap G2, §11.6).
- UTC comes from the grandmaster; NTP is only a bench convenience. Bench PTP time is UTC (`phc2sys -O 0`), not TAI as gPTP expects; to be fixed with the gPTP configuration (A11).

### 4.3 Session plan and index

- At arming, the controller (bench: the laptop) sends **one plan to all nodes**: `{session, T0, period}`. **T0 is in the future** (e.g. now + 1 s, rounded), so the network/Linux jitter of the command (ms) does not matter; it only has to arrive before T0.
- **Index definition:** `index = round((t_edge − T0) / period)`, computed per edge from its hardware timestamp. **Never counted.**
- **Late node** (command after T0, or a reboot mid-session): it joins the grid at the next edge `T0 + k·period` with index `k` and reports frames 0..k−1 as missing. It stays aligned.
- Per-session index starts at 0. **Same (session, index) on every node = the same edge.**
- What remains is the real sync error: PTP offset (sub-µs to µs), edge generation (ns), cabling (~5 ns/m), sensor reaction (fixed). It is measured as the timestamp difference per index, against the budget (**TBD**, §11.4).

### 4.4 Frame-to-trigger matching and drop detection [OURS]

- `trigd` passes `{index, t_edge}` to `nodecap`. The edge event arrives before its frame (exposure + readout come first), so `nodecap` keeps a small list of undecided edges.
- **Match by time, not by queue position (DL-27, implemented for `emu`):** a frame belongs to the edge whose `t_edge + latency` is nearest, within **± 3 ms** (`--cam-window-ms`; refused at ¼ period or more). Two frames for one edge: the nearer kept, the other spurious. An edge is decided once its window has passed (a later frame arrived, or max(4 periods, 100 ms) without frames).
  - Latency configured (`--cam-latency-ms`, 25 ms); measured min/mean/max reported; warning when frames sit beyond half the window. Measuring it at start: later.
  - Why not ± half a period: a frame ¾ period late lands ¼ period from the next edge and takes its index if that edge's frame is missing (seen in the emulator stress run).
  - Limit: timing alone can't be fully safe (a stray frame exactly where a missing one belongs); the sensor frame counter is the real fix (below).
- Clocks: edges are in PHC time, frames in CLOCK_MONOTONIC. Converted per edge: offset from bracketed reads of `/dev/ptp0` and CLOCK_MONOTONIC (best of 5), µs-level.
- **Edge without frame** = drop: gap recorded (manifest, health, event), index never reused. **Frame without edge** = spurious: counted, not encoded. Camera sequence jumps counted next to gaps.
- **Verified** with the emulator (`tests/camemu-check.sh`): board, 60 fps, 1920x1200, Wave5, 600 edges: clean 600/600 bound, latency 24.5-25.5 ms; with 3 % drop/extra/late faults every count matched the emulator's plan and no frame got a wrong index (drawn index = SEI index).
- **Second check point, the encoder output:** every frame enters the encoder with a unique PTS = edge time − T0 (DL-15); at the output, index = `round(PTS / period)`, looked up in a table index -> `{session, edge time, flags}`. Index missing = gap, repeated = duplicate, no table entry = frame without edge. Needed because Wave5 was observed to lose and repeat a timestamp at stream start (P-1). **Implemented** in `nodecap`.
- With a camera: the **sensor frame counter** must also advance by 1 per matched pair (third check).
- `test` source: frames are created from edges, so binding is by construction; drops are **injected on purpose** (`trigd --test-skip`) to test detection. Each test frame also carries its index **drawn into the picture** (DL-17), so a decoded frame proves its SEI sits on the right frame.
- SEI per frame (prefix SEI, `user_data_unregistered`, our UUID; inserted by our code before the first slice, DL-16): `{version, session, flags, index, t_edge}`. A segment file copied anywhere still proves its own timing and gaps. Checked on the laptop by `sei-dump` (one SEI per frame, continuity, drawn index = SEI index).

---

## 5. Software on the node

### 5.1 Processes [OURS unless noted]

| Process | Language | Role |
|---|---|---|
| `trigd` | C | arms CPTS GENF at T0, reads HW_TS_PUSH events, computes indices, publishes `{index, t_edge}`. **done:** measured edge times from the CPTS loop (G1), computed fallback |
| `nodecap` + `recorder` | C, **one process** | GStreamer pipeline (elements [TI]); matching, drop detection, SEI insertion; segments, manifest, ring, locks (separate module) |
| `nodeagent` | C | control interface (§6) on `libmicrohttpd`, segment fetch. **v1 done:** the only long-running process; starts `trigd` + `nodecap` per session (DL-23) |
| `ptp4l`, `phc2sys` | linuxptp | time |

- **As built:** `nodeagent` runs as the systemd service `fwtrial-nodeagent` and starts `trigd` + `nodecap` per session (DL-23); `ptp4l` as `fwtrial-ptp4l` (DL-25). Product: each as its own service with restart (DL-12, DL-14); health over a local socket to `nodeagent`.
- `nodecap` and `recorder` share one process so encoded frames reach the recorder without inter-process transfer. The recorder stays a separate module (plain functions), ready to become the `segmentsink` element.

- **Language: C** for all on-node code (why: decision log DL-02). Libraries: `libmicrohttpd` 12.62.1 (in the SDK sysroot and on the board image); zcbor vendored (§6).

### 5.2 Sessions and segments

- **Session number:** part of the session plan, made by the **single plan maker** (controller in the product; the laptop script or operator in the trial) and the same on every node of a pack. **Nodes never make one** (DL-20): `trigd` requires `--session`; the recorder refuses a number already on disk. Index restarts at 0 per session. Statuses: `recording`, `complete`, `interrupted`; `truncated by ring` when segments were reclaimed.
- **Segment length: 4 s** (DL-07); ~2.5 MB @1080p30, ~5 MB @1080p60. A segment ends at the **first keyframe at or after** its boundary index (k·120 @30 fps) and is named by its real first index (DL-18): after a dropped frame the encoder's keyframes shift, so a segment may run up to 1 s longer.
- A new session always starts a new segment; the last segment of a session may be short.
- A pause between sessions is not a drop; gap detection runs within a session.

```
/rec/
  manifest.bin                       append-only change log (the truth)
  s0534242870/0000000000.h265        session 534242870, segment starting at index 0
  s0534242870/0000000120.h265
  s0667108096/0000000000.h265        next session
```

- **Write discipline:** flush (`fdatasync`) at every keyframe, so a crash loses at most ~1 s. On segment close: fsync the file and its directory, *then* append its manifest record + fsync.
- **As built:** `src/recorder/`, plain functions inside `nodecap` (`--rec /rec`); frames are handed over without copying (head, SEI, rest). Checked by `tests/recorder-check.sh` on laptop and board.

### 5.3 Manifest and index

- **Records (DL-19):** fixed 128 bytes: header (magic, version, type, sequence number, session), type-specific body, CRC-32. Types: session start, session end, segment closed, segment deleted, lock, unlock. Segment record: `{first_idx, last_idx, t_first, t_last, bytes, frames, missing, up to 8 keyframe byte offsets + their indices, flags (recovered)}`. Locks are separate records (ranges), not segment flags. 24 h = 21,600 segment records ≈ 2.8 MB.
- **Torn write:** a record half-written at power loss fails its CRC; loading stops there and the next start cuts the tail off.
- **The manifest on eMMC is the truth:** every change (segment closed, reclaimed, locked) is appended immediately, then applied in RAM. **The RAM index is a mirror**, loaded once at boot. The manifest is compacted occasionally (new file + atomic rename).
- **Queries never scan the eMMC:** sessions and segments come from RAM; fetch reads only the needed files. As built: linear scans of the RAM arrays (fine at trial scale); binary search over the sorted segment list when needed (~15 steps for 24 h). Manifest compaction: not built yet.
- **One owner:** the recorder alone owns `/rec` and its index (exclusive `flock` on the manifest; a second recorder is refused).
- **Control socket** (as built): `/run/fwtrial/rec.sock`, Unix datagram, fixed 120-byte versioned struct (same pattern as the trigger message, DL-11): lock and unlock requests, served by `nodecap` while recording. `rec-tool` uses it; with no recorder running it opens `/rec` itself. `nodeagent` will use the same socket (plus queries: session/index/time range -> files + keyframe byte offsets).
- No database (SQLite considered and rejected: DL-06).

### 5.4 Ring and locks

- **The ring is ordinary segment files on ext4 plus a delete rule**, not a special circular area. Large sequential writes and whole-file deletes suit the eMMC's own wear levelling (AC §5).
- **Before each new segment:** make room for 1.5 × the expected segment size (the last closed segment; before the first one: bitrate × length) + `reserve_bytes` (64 MB). Free = min(filesystem free, `ring_max_bytes` − used). A small `ring_max_bytes` makes the ring wrap within a test run (`--ring-max-mb`).
- **Victim order:**
  1. oldest **closed** session, deleted **whole** (its unlocked segments)
  2. oldest unlocked segment of the **running** session
  3. none -> the frame is not recorded and counted (`storage_full` event with `nodeagent`); unreachable while the lock cap holds
- **Delete order:** manifest "deleted" record + fsync -> unlink file -> update RAM.
- **Locks:** whole segments, with reason and TTL, recorded in the manifest. A lock can cover a **post-event window**: the past part locks immediately, the future part is applied to segments as they close. A lock is **refused and reported** if locked bytes would exceed `lock_cap` (~20 % of the ring), so the ring can always free space and recording never stops because of locks. Expired locks become reclaimable.
- **Lock expiry** uses the wall clock (`CLOCK_REALTIME`): it depends on the node's time being set (open item: RTC/NTP on the board).
- **Truncated sessions** stay listed with their available ranges. A session disappears only when its last segment is gone.
- **Verified** (laptop and board, 4 MB ring): 900 frames recorded while wrapping, closed session reclaimed first, locked segment survived, oversized lock refused at the cap, disk use under the limit.

### 5.5 Fetch

- By session and time or index:
  ```
  GET /v1/fetch?session=7&last_s=90
  GET /v1/fetch?session=7&from_idx=2700&to_idx=5399
  GET /v1/fetch?from=<t>&to=<t>
  ```
- **Keyframe-aligned:** reading starts at the first keyframe at or after the requested start (precision ≤ keyframe interval, 1 s), using the keyframe byte offsets in the manifest.
- The response states the **actual range** returned and lists any **missing ranges** (gaps, truncation). Nothing is shortened silently.
- A running session includes its open segment up to the last flushed keyframe.
- Joined segments form a valid H.265 stream. Frame-exact trimming, if needed, is done by the client using the SEI.

### 5.6 Reset and recovery

- The eMMC keeps everything. At start: load the manifest into RAM (cut a torn tail); check the directory of every session without a session-end record (normally only the last one):
  - a file without a record is indexed from its SEIs; complete keyframe groups are kept (everything before its last keyframe), the rest is cut off; a file without a complete group is removed
  - a "deleted" record whose file still exists -> the file is removed
- The session is marked `interrupted`. **No automatic re-arm**: the controller decides (a node re-arming on its own could diverge from its pack). The `rebooted` event comes with `nodeagent`.
- **Verified** (laptop and board): `kill -9` mid-session -> restart -> open segment rebuilt, session `interrupted`, `rec-tool verify` clean.

### 5.7 eMMC layout

- **Trial** (boots from SD): `mmcblk0p1` = `/rec`, whole eMMC (~29.6 GB): wiped 2026-11-13, one GPT partition, ext4 labelled `rec`, mounted via `fstab` (`noatime`, `nofail`). Measured (`dd`, sanity check only): read 173 MB/s, write 96.5 MB/s sequential; 4 KiB writes 36.6 MB/s. Need: 0.6-1.2 MB/s.
- **Product** (boots from eMMC):

| Area | Size | Content |
|---|---|---|
| boot0 / boot1 (eMMC hardware) | 31.5 MB each | boot loaders |
| RPMB (eMMC hardware) | 4 MB | secure storage, identity keys |
| p1 rootfs A / p2 rootfs B | ~2 GB each | A/B system |
| p3 `/data` | ~256 MB | config, logs, offload state |
| p4 `/rec` | rest | segments + manifest |

- `/rec` is a dedicated real partition (never tmpfs). Recordings can't starve the system, and updates never touch them.
- **Ring depth on the EVM** (~25 GB usable): ~11 h @1080p30, ~5.5 h @1080p60. Product: 8 h @30 ≈ 18 GB; 24 h @60 ≈ 108 GB (AC §2 storage fits).

### 5.8 Enhancement (not in the trial slice): network offload

- **Pull model:** a PC fetches via the existing API, then confirms with `POST /v1/ack`. The node needs no upload client.
- The manifest flag `offloaded` (in the format from the start); reclaim prefers offloaded segments.
- Health reports the offload backlog; upload must outpace recording (AC §5 dock rule).
- No copy to SD (AC §5 rejects SD).

---

## 6. Control interface

**Contract:** `icd/fwt-icd-v1.cddl` (CDDL, ICD version 1): every message, the endpoint list and the rules (version field, key order, units). Implemented by `nodeagent` (`src/nodeagent/`), checked by `tests/nodeagent-check.sh` on laptop and board.

- **Transport (trial): HTTP** on `libmicrohttpd`, versioned (`/v1/…`, an `icd` version field in every message; minor = additive only).
- **Encoding: CBOR** (RFC 8949), `Content-Type: application/cbor`. **The node speaks CBOR only.**
- **Schema: CDDL** (RFC 8610), one `.cddl` file per ICD version, in the repo. It is the contract.
- **Code: zcbor** generates the C encode/decode functions (with validation) from the CDDL. The generated code and zcbor's small runtime are vendored into the repo. The same library runs on the Linux node and on Zephyr (zcbor is a Zephyr module).
- **Debugging: JSON on the laptop only.** CBOR maps 1:1 to JSON, so a laptop tool shows responses as JSON and turns JSON input into CBOR (`curl … | cbor2json`, or an `icdctl` CLI). No JSON code on the node.
- **Fetch:** HTTP with range requests, node -> PC directly; the controller never carries video. Segment bytes are raw H.265, not CBOR.
- **Events** (node -> controller): Server-Sent Events carrying CBOR (**TBD**).
- **Product direction:** CoAP instead of HTTP for control and events, because the controller is a Zephyr MCU; same CBOR messages, only the transport changes. Fetch stays HTTP.

Why CBOR/CDDL/zcbor and HTTP/CoAP, and what was rejected: DL-03, DL-04.

| Group | Endpoints / content | v1 status |
|---|---|---|
| Identity | `GET /v1/identity`: node id, software version, ICD version, fps range, max resolution | **built** |
| Session | `POST /v1/arm {session, fps, t0?}` -> `{session, t0, period}`; `POST /v1/disarm`. One session at a time (409 `busy`); reused session number refused (409 `session-exists`, DL-20). `nodeagent` starts `trigd` + `nodecap` per session (DL-23) | **built** |
| Storage | `GET /v1/sessions[?after=]`, `GET /v1/segments?session=[&from_idx=]` (pages of 512), `POST /v1/lock`, `POST /v1/unlock`, `GET /v1/fetch` | **built** |
| Health | `GET /v1/health`: state (idle/recording/failed + message), ring used/locked/free, depth in seconds at the measured bitrate, eMMC wear + pre-EOL, frames/missing of the session, SoC temperature, PHC time, `time_verified` | **built** (`time_verified` always false: DL-13) |
| Configure | exposure, gain, WB, bitrate, trigger source | deferred (needs a camera) |
| Time | PTP offset, lock state | deferred (DL-13) |
| Events | drop, trigger lost, storage nearly full, lock refused, rebooted | deferred (SSE or CoAP) |

**As built (v1):**
- **Fetch** `GET /v1/fetch?session=S&from_idx=I[&to_idx=J]` or `&last_s=N`: raw H.265 (`video/H265`), streamed from the segment files, starting at the first keyframe at or after the requested start. Headers: `X-Fwt-From` / `X-Fwt-To` (actual range), `X-Fwt-Missing` (requested but not returned: before the keyframe, drops between segments, reclaimed segments, not yet closed), `X-Fwt-Missing-Inside` (frames missing inside returned segments), `X-Fwt-Open-Segment` (`not-included` while recording). At most 600 s per request.
- **Reads are read-only** on `/rec` and answer immediately with the last known good state (DL-22); the running session's open segment (at most 4 s) is not included yet.
- **Locks** go to the running recorder over its control socket, else `nodeagent` opens `/rec` itself.
- **Errors:** HTTP status + CBOR `{icd, code, message}` (`bad-request`, `busy`, `session-exists`, `not-armed`, `no-data`, `no-session`, `no-lock`, `lock-cap`, `range-too-long`, `gone`, `not-found`).
- **Safety:** GET never changes state; strict query parsing; CBOR bodies decoded by the generated code (anything not in the schema is rejected); body limit 4 KB; at most 4 fetches at a time, 16 connections, 30 s connection timeout; a segment opened for fetch stays readable if the ring deletes it.
- **Key order:** the generated decoder expects map keys in the CDDL order; clients send requests in that order.
- **Laptop:** `curl` + `tools/icd-json` (CBOR <-> JSON, Python, laptop only).
- **Not built yet:** authentication (closed bench network; product: needed), HTTP range requests, the open segment in fetch, events, configure, CoAP, the node simulator and conformance suite.

**Failure behaviour:**
- Controller lost: keep recording, queue events.
- Trigger lost: flag segments `untriggered`; invent no indices.
- Storage full: §5.4.

A software **node simulator** serves the same interface, so the conformance suite runs without hardware.

---

## 7. Controller and supervisor (design only, not on the bench)

- **Controller (B3, nRF54L15 + LAN9668, Zephyr):** fleet registry, session plans `{session, T0, period}`, gPTP time master via the LAN9668, command fan-out, health aggregation. Never in the video path.
- **Start of its code (bench):** `fwt-rec` (DL-28), one node, one recording: `ctl_node` (ICD protocol via zcbor, no I/O) is meant to carry over; `ctl_http` (libcurl) is replaced by the controller's transport.
- **Supervisor MCU (lite nodes, nRF54L15, Zephyr):** FSIN generate/receive + timestamp, trigger index to the SoC, IMU over SPI, power sequencing, watchdog, rollback, identity. On our J722S node the CPTS does the trigger job, so no MCU (see §11.4).

---

## 8. Node conformance requirements (interchangeability)

Another node may use entirely different code for camera, ISP, encode and timing. It is interchangeable if it meets all of these:

| # | Requirement | Test |
|---|---|---|
| R1 | Implements ICD version X.Y (CBOR, per the CDDL schema): all commands, health fields, events, error codes | conformance suite validates every message against the CDDL |
| R2 | Exposure starts on the trigger edge (slave mode); no free-run while armed | logic analyzer: trigger vs sensor strobe |
| R3 | Index = `round((t_edge − T0)/period)` from a hardware edge timestamp; late join on the grid | suite + PPS comparison |
| R4 | Every frame carries the SEI `{session, index, t_edge}` | stream parser |
| R5 | Edge timestamp vs grandmaster within the budget (**TBD**, §11.4) | HIL |
| R6 | Drops/duplicates detected and reported, never hidden | fault injection |
| R7 | Session/segment/manifest semantics of §5 (4 s segments, keyframe-aligned fetch, locks, truncation) | list/fetch/lock round trip |
| R8 | Failure behaviour of §6 | fault injection |
| R9 | Capture spec of AC §2: 1920x1200, 30/60 fps, H.265 Main 8-bit, manual exposure/gain/WB | suite + bitstream check |

### 8.1 What changes per target

| Component | Own J722S/AM62A carrier | AM62A3 (standard tier) | RV1106 + nRF54 (lite tier) |
|---|---|---|---|
| Sensor driver | reuse | reuse | port to the Rockchip kernel |
| Overlay / pinmux | rewrite [EVM] | rewrite | rewrite |
| ISP path (TIOVX) | reuse | reuse (smaller VPAC) | replace (RKAIQ) |
| Encoder element | reuse | reuse (same Wave5) | replace (Rockchip MPP) |
| Trigger (CPTS, `trigd`) | reuse, new pins | reuse | moves to the supervisor MCU |
| recorder, manifest, nodeagent, SEI, matching | reuse | reuse | reuse (performance check on a single A7) |
| Build (Yocto layer) | new machine | new machine | different BSP |

---

## 9. Build and deploy

- **Image:** Yocto scarthgap, TI config `processor-sdk-analytics-11.02.02` + our layer `meta-fwtrial` (services, configs, overlay, driver), pinned with kas.
- **Now (bench), as built (DL-24):** `deploy/deploy.sh <user@node>`: cross-build with the installed SDK (CMake toolchain file), bundle `fwtrial-<version>-aarch64.tar.gz`, install on the board running the 11.0 image (user space compatible, §1.2) into `/opt/fwtrial/releases/<version>` with an atomic `current` symlink, previous release kept for `install.sh --rollback`, systemd units enabled at boot. Version = project version + git commit. Verified: deploy, session through the installed service, reboot (services back by themselves). Commands: `docs/setup.md` §8.6. Check scripts copy their own build to `/tmp` and pause the installed service during a run.
- **Operators (DL-29):** a host bundle (`deploy/make-host-bundle.sh`) carries the node bundle + `deploy.sh` + `fwt-rec`, `sei-dump`, `fwt-doctor`: deploy, record and check without build tools (`docs/setup.md` §8.7). `tools/fwt-doctor host | host-dev | dev` checks each machine read-only before anything runs; the check scripts use it as their pre-check.
- **Services (as built):** `fwtrial-nodeagent` (the only long-running process of ours, DL-23; `trigd --disarm-only` before start and after every stop, DL-14; waits for `/rec`; restart on failure) and `fwtrial-ptp4l` (PTP client only, DL-25).
- **Build system: CMake** (DL-05): laptop build for unit tests, board cross-build via `cmake/j722s-sdk.cmake` (SDK compiler + sysroot); zcbor code regenerated from the `.cddl`; `ctest`; Yocto `cmake` class later.
- **Kernel work** (overlay, driver) needs the matching kernel: install the SDK 6.12.57 kernel + modules + DTBs, or flash a fresh 11.02 image to a spare SD.
- **Update/rollback (product):** A/B rootfs on eMMC (§5.7), boot counting in U-Boot (e.g. RAUC). **TBD**. The trial's release directories + symlink are the same idea for our software only.
- **Secure boot/identity:** board is HS-FS; product uses HS-SE with our keys; per-device key in OP-TEE / eMMC RPMB. **TBD**.

---

## 10. Test and measurement

### 10.1 Measurements on this node

Full list with commands: the bench measurement log (`0004-measurements-to-do.txt`, kept outside the repo).

| # | What | Needs |
|---|---|---|
| M1/M2 | eMMC read speed, identity, wear | nothing (read-only) |
| M3 | eMMC write speed (need 0.6-1.2 MB/s) | `/rec` partition |
| M4 | Trigger interval jitter (CPTS timestamps; later a logic analyzer on J1 pin 18) | `trigd` measured; first numbers: interval = period within 2 ns (300-600 edges); 30-min run with PTP locked todo |
| M5 | PTP offset to the laptop | `ptp4l` |
| M6 | Encoder bitrate and CPU @30/@60 | encode pipeline |
| M7 | Frames vs drops, 30+ min, with injected drops | test source + matching |
| M8 | Ring reclaim and lock vs reclaim (small ring, wraps in ~13 min) | recorder |
| M9 | "Last 90 s" query time with a full manifest | recorder + fetch |
| M10 | Recovery: power pull, Ethernet unplug, process kill | recorder + nodeagent |
| M11 | Binding by time with the camera emulator (60 fps, faults) | `cam-emu`; **done** |

### 10.2 Multi-camera sync: what is shown vs described

| Level | Proves | Needs | Trial |
|---|---|---|---|
| 1 | Two trigger generators fire on the same grid | one board: 2 CPTS outputs -> 2 timestamp inputs | shown (if the pins allow) |
| 2 | Two nodes (packs) aligned over PTP: same (session, index) and segment numbers, edge skew per index | second board + switch (PTP-aware for best accuracy) + logic analyzer | described; one node locked to the laptop shown (M5) |
| 3 | Two sensors expose at the same instant | two trigger-capable cameras (one board: J1 + J2 = pack case) + strobe measurement | described |

### 10.3 Automation

- **Host:** unit tests, node simulator, conformance suite, recorder fault injection (power loss, full disk).
- **HIL:** N nodes + logic analyzer / time-to-digital converter on trigger and strobe lines -> skew/jitter histograms.
- **CI rig:** nightly multi-node run; pass/fail on the skew budget, zero silent drops, recovery after unplug.

---

## 11. Plan, risks, open questions

### 11.1 Components and critical path

| # | Component | Camera needed? | Status (2026-11-13) |
|---|---|---|---|
| A1 | Encode pipeline (test video -> Wave5) | no | **done** (`nodecap`, board) |
| A2 | `trigd`: CPTS edges + hardware timestamps | no | **done:** hardware edges, hardware-measured times (overlay, G1 closed); computed fallback |
| A3 | Trigger-driven test source | no | **done** (drawn index) |
| A4 | SEI per frame | no | **done** (+ `sei-dump`) |
| A5 | Recorder: segments, manifest, ring, locks | no | **done** (`src/recorder`, `rec-tool`, `recorder-check.sh`) |
| A6 | Query/fetch | no | **done** (sessions, segments, keyframe-aligned fetch) |
| A7 | `nodeagent` control interface | no | **done** (HTTP + CBOR, ICD v1; events and configure deferred) |
| A8 | Matching + drop detection | no | **encoder-output check done**; camera-side matching with B |
| A9 | Node simulator + conformance tests | no | open |
| A10 | Soak and fault tests | no | open; check scripts exist (`tests/*-check.sh`) |
| A11 | PTP with the laptop | no | bench check done (P-3); runs at boot as `fwtrial-ptp4l` (client only); gPTP profile and lock check (DL-13) open |
| B1-B5 | Sensor driver, overlay, ISP path, real binding, strobe measurement | yes | blocked (G3) |

- **Order:** A2 -> A1 + A3 + A4 -> A5 + A6 -> A7, A8 -> A10 -> A9 / A11.
- **Critical path for real video:** camera connected -> driver + overlay -> ISP -> binding. B replaces A3; everything else stays.

### 11.2 Milestones

**TBD:** 4 weeks / 3 months / 6 months.

### 11.3 Top 5 risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | Sensor driver + ISP tuning on TI (AR0235 not in TI's list) | Trial on a supported camera; AR0234 as stepping stone; TI FAE early |
| 2 | Trigger pin routing (GENF to J1 pin 18) and camera trigger support | Check the pin mux now; DMTimer + PTP or jumper as fallback; camera chosen by trigger support |
| 3 | Frame-to-trigger binding under load | Time-based matching, three checks, injected drops, soak |
| 4 | Second node implementation diverges | §8 requirements + conformance suite + simulator |
| 5 | eMMC endurance 24/7 | Sizing, EXT_CSD monitoring (M2), industrial parts |

### 11.4 Positions on AC §12

1. **Silicon:** **TBD**. Note: the J722S drives four sensors from one CPTS edge, which favours "several cameras per node" for a pack (§10.2 level 3).
2. **Supervisor MCU on the standard tier:** **TBD**. On J722S/AM62A the CPTS already does the trigger job (§4.1).
3. **Sync budget:** **TBD**. Proposal: at most 50 µs camera-to-camera (hands at 2 m/s move 0.1 mm in 50 µs).
4. **Ring depth/bitrate:** §5.7 gives the numbers per storage size; **TBD** as a product commitment.
5. **Lite-tier controller link:** **TBD**.
6. **Updates/identity:** §9.

### 11.5 Disagreements with the Architecture Context

| # | AC position | Ours | Cost | Benefit |
|---|---|---|---|---|
| 1 **Changed** | Retrieval: "the controller is a client of N small file servers and forwards onward to a PC" (AC §5) | The PC fetches **directly from the nodes** through the switch. The controller orchestrates (sessions, locks, health) and never carries video bytes. | The PC needs a route to every node; access control moves to the nodes. | An nRF54 controller can't forward ~100 Mbps. Its load stays bounded by control traffic, which serves the AC's own goal of a small controller (§6, §7). |
| 2 **Refined** | Each frame carries a trigger counter; "the trigger source logs the counter against UTC" (AC §4) | The index is **computed from the hardware edge timestamp**, `round((t_edge − T0)/period)`, against a shared session plan `{session, T0, period}`. Never counted. | Every node needs a PTP-locked clock with hardware edge timestamps (R3, R5). | A missed event can't shift later indices; a late or rebooted node rejoins on the grid; the same (session, index) on every node with no central counter log (§4.3). |
| 3 **Kept, verified** | No supervisor MCU on the standard tier, because TI has 1588 built in (AC §3) | Kept. The J722S CPTS has 2 periodic outputs and 4 timestamp inputs (checked on the board), and the timesync router can loop GENF1 into HW3_TS_PUSH: sufficient to generate and timestamp the trigger in hardware (§4.1; route needs an overlay, G1). | Depends on device-tree routing: the loop (G1) and a CPTS output to the camera pin (risk 2, §11.3). | No second codebase or MCU cost on the standard tier. |

### 11.6 Known gaps

| # | Gap | How we found it | Approach chosen | Until closed |
|---|---|---|---|---|
| G1 | **Closed 2026-11-13.** **No GENF -> timestamp-input route on the current image.** The board's 11.0 device tree has no timesync router node and no `ti,pps`; the GENF1 -> HW3_TS_PUSH loop exists only in the 11.02 SDK EVM tree. Without it, `trigd` cannot timestamp its own edges. | Pre-coding check P-2: periodic output 1 ran, but no events arrived on any of the 4 timestamp inputs; the live device tree confirmed the missing router. | **Device-tree overlay** on the unmodified base tree, loaded by U-Boot (`name_overlays`): adds the timesync router (`reg-mux`) with `mux-reg-masks-state = <0x34 0x0001ffff 0x10011>` (output 12 = HW3_TS_PUSH <- input 17 = GENF1), without `ti,pps` so `trigd` owns GENF1. Board kernel supports it (`CONFIG_MUX_MMIO=y`, TI `mux-reg-masks-state` extension present). Source: `dts/k3-j722s-evm-fwtrial-cpts-loop.dtso`, not yet deployed. Rejected: booting a decompiled live tree (bakes in U-Boot fixups, replaces the reference tree), the 11.02 kernel (larger change), a physical wire (board modification). | `trigd` runs in **computed-time** mode (`t_k = T0 + k·period`). Edges are still generated in hardware on PTP time, but M4 (jitter) and per-edge proof that each edge fired need **measured** mode, i.e. the overlay. Downstream code is unaffected: it only sees `{session, index, t}`. |
| G2 | **Absolute PTP accuracy not measured; bench grandmaster is a weak clock.** P-3 measured offset and stability (30 ns RMS), but PTP cannot see one-sided delays: part of the +7.5 µs path delay may be asymmetric latency inside the laptop's NIC, which would leave a constant offset of up to a few µs. The laptop's Intel I219 clock runs ~1600 ppm off and only works as grandmaster with `phc2sys`. P-3 used the default 1588 settings, not the gPTP profile. | P-3: the first run (without `phc2sys`) failed (board frequency pinned at the +488 ppm limit, negative path delay); comparing each clock to its own NTP-synced system clock identified the laptop. | Keep the laptop + `phc2sys` as bench grandmaster; report jitter and stability as **measured**, absolute alignment as **not measured**. Product: a real grandmaster, time-aware switches, gPTP profile, one-time calibration per hardware type against PPS + scope (DL-01). | No effect on a single node: its edges, timestamps and indices are consistent on its own clock. Matters only for cross-node alignment (described, not shown). |
| G3 | **Camera not detected.** The bench AR0234 module answers on no EVM camera connector; the image has no AR0234 driver or overlay (only IMX219, OV5640). | Board side checked: connector mux, enable pins (GPIO mode, driven high), I/O level 3.3 V (SW2.4), trace mode off, pinout = Raspberry Pi 22-pin; I2C scan of all four connector buses finds nothing. | Remaining causes are physical: cable type, module pinout, power at the module. Next: multimeter at the module, other cable type, module documentation, or a known-good Raspberry Pi camera (IMX219) to prove the board path. | Slice runs on the trigger-driven test source (DL-10). Even once detected: driver port + overlay, build against the board kernel (6.12.17) or move to 11.02, ISP tuning (colour). |
