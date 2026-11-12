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

- Product sensor: onsemi AR0235. Bench module believed AR0234 (same family) *(verify)*.
- **Camera choice rule for the trial:** a camera with an existing driver in the TI SDK, preferably one whose sensor and driver support an external trigger. Without trigger support, the trigger chain is proven to the connector pin with the test source, and the gap is stated.
- **The AR0235 driver and ISP tuning are ours** (AC §7, §9) and a later milestone: V4L2 subdev with trigger (slave) mode, manual exposure/gain/white balance, frame counter. The overlay is based on `k3-j722s-evm-csi2-quad-rpi-cam-imx219.dtso`.

---

## 3. Data path

```
trigger-driven test source (now) │ AR023x -> CSI2RX3 -> v4l2src -> tiovxisp (camera, later)
      -> NV12 1920x1200 -> v4l2h265enc (Wave5) -> h265parse -> appsink
      -> our code: SEI {session, index, t} + drop check -> recorder -> eMMC
```

**GStreamer** is the media framework [TI elements, standard framework]:
- **Now:** a thin pipeline; our code sits at `appsrc` (one frame per trigger edge) and `appsink`.
- **Later:** the same logic wrapped as our own elements (`trigsrc`, `trigsei`, `segmentsink`), so the whole node is one portable pipeline. On Rockchip only the middle changes (`mpph265enc`).
- Logic is written as plain functions (SEI building, drop check, segment/manifest writing) so it can be wrapped without rewriting.
- The trigger PHC time is carried as the buffer timestamp through the encoder *(verify Wave5 keeps it 1:1 per frame)*.

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
- Each edge is **timestamped in hardware** on a CPTS input (HW_TS_PUSH). On the EVM, GENF1 -> HW3_TS_PUSH is already routed (`ti,pps = <2 1>`), so timestamps need no extra wiring. `trigd` reads them via `PTP_EXTTS_REQUEST`. A late read delays *when* we learn an edge, never *what time* it carries.
- When `ptp4l` adjusts the clock, the CPTS adjusts the edge frequency in hardware: edges stay locked to network time.
- **Route to the sensor:** GENF -> pin mux (our overlay) -> SoC pad GPMC0_AD7 -> J1 pin 18 -> module -> sensor trigger input.
  - **Open:** can GENF (or a timer output) be muxed onto GPMC0_AD7? *(verify in the J722S pin mux table.)* If yes, it's an overlay only. If only a DMTimer output is available, discipline it from PTP via the timesync router. If neither, a jumper wire from a GENF-capable pad (ask Matthew).
  - The I/O level (SW2.4) must match the module.

### 4.2 Time base

- PHC (/dev/ptp0) is the reference; `ptp4l` (gPTP profile) on eth0, `phc2sys` keeps CLOCK_REALTIME aligned.
- Bench: laptop Intel port (enp0s31f6, hardware timestamping) as grandmaster. Product: the controller's TSN switch (AC §8).
- UTC comes from the grandmaster; NTP is only a bench convenience.

### 4.3 Session plan and index

- At arming, the controller (bench: the laptop) sends **one plan to all nodes**: `{session, T0, period}`. **T0 is in the future** (e.g. now + 1 s, rounded), so the network/Linux jitter of the command (ms) does not matter; it only has to arrive before T0.
- **Index definition:** `index = round((t_edge − T0) / period)`, computed per edge from its hardware timestamp. **Never counted.**
- **Late node** (command after T0, or a reboot mid-session): it joins the grid at the next edge `T0 + k·period` with index `k` and reports frames 0..k−1 as missing. It stays aligned.
- Per-session index starts at 0. **Same (session, index) on every node = the same edge.**
- What remains is the real sync error: PTP offset (sub-µs to µs), edge generation (ns), cabling (~5 ns/m), sensor reaction (fixed). It is measured as the timestamp difference per index, against the budget (**TBD**, §11.4).

### 4.4 Frame-to-trigger matching and drop detection [OURS]

- `trigd` passes `{index, t_edge}` to `nodecap`. The edge event arrives before its frame (exposure + readout come first), so `nodecap` keeps a small queue of recent edges.
- **Match by time, not by queue position:** frame ↔ edge k if `t_frame − t_edge(k) ≈ expected latency (± tolerance)`. The latency (exposure + readout + capture, tens of ms) is measured at start; edges are 16.7-33.3 ms apart, so the match is unambiguous.
- Clocks: edges are in PHC time, V4L2 frames in CLOCK_MONOTONIC. Converted via `phc2sys` / the PTP system-offset query (needs well under 1 ms).
- **Edge without frame** = drop: gap recorded (manifest, health, event), index never reused. **Frame without edge** = spurious: flagged.
- With a camera: the **sensor frame counter** must also advance by 1 per matched pair (third check).
- Test source: frames are created from edges, so binding is by construction; drops and duplicates are **injected on purpose** to test detection.
- SEI per frame (`user_data_unregistered`, our UUID): `{session, index, t_edge}`. A segment file copied anywhere still proves its own timing and gaps.

---

## 5. Software on the node

### 5.1 Processes [OURS unless noted]

| Process | Language | Role |
|---|---|---|
| `trigd` | C | arms CPTS GENF at T0, reads HW_TS_PUSH events, computes indices, publishes `{index, t_edge}` |
| `nodecap` + `recorder` | C, **one process** | GStreamer pipeline (elements [TI]); matching, drop detection, SEI insertion; segments, manifest, ring, locks (separate module) |
| `nodeagent` | C | control interface (§6) on `libmicrohttpd`, segment fetch |
| `ptp4l`, `phc2sys` | linuxptp | time |

- Each runs as a systemd service with restart. Health goes over a local socket to `nodeagent`.
- `nodecap` and `recorder` share one process so encoded frames reach the recorder without inter-process transfer. The recorder stays a separate module (plain functions), ready to become the `segmentsink` element.

**Language decision: C for all on-node code.**
- **Chosen:** our code is on the data path; GStreamer's native API is C; the SDK cross-toolchain is ready; small footprint, which also fits the lite tier (RV1106: one A7 core, 128-256 MB RAM).
- **Considered, Rust:** likely the right long-term choice (memory safety, official GStreamer plugin support via `gst-plugins-rs`), but its cross-toolchain setup is too long for the prototype. Noted as the product option.
- **Rejected, Python** (on the node): slower, less control, much more CPU and memory than a firmware system should spend. Laptop test tools are not firmware; their language is separate.
- **Libraries:** `libmicrohttpd` 12.62.1 is in the SDK sysroot and on the board image (dynamic link, nothing to copy). zcbor is vendored (§6).

### 5.2 Sessions and segments

- **Session:** assigned at arming (by the controller in the product, shared by all nodes). Index restarts at 0 per session. Statuses: `recording`, `complete`, `truncated`, `interrupted`.
- **Segment length: 4 s.** Power of 2; whole frame counts (120 @30 fps, 240 @60 fps), so segment k starts at index k·120 (k·240); ~2.5 MB @1080p30, ~5 MB @1080p60. Within the AC §5 range of 1-10 s.
- A new session always starts a new segment; the last segment of a session may be short.
- A pause between sessions is not a drop; gap detection runs within a session.

```
/rec/
  manifest.bin                 append-only change log (the truth)
  s0001/000000000.h265         session 1, segment starting at index 0
  s0001/000000120.h265
  s0002/000000000.h265         session 2
```

- **Write discipline:** flush (`fdatasync`) at every keyframe, so a crash loses at most ~1 s. On segment close: fsync the file, *then* append its manifest record + fsync.

### 5.3 Manifest and index

- **Record per segment:** `{session, seg, first_idx, last_idx, t_first, t_last, bytes, keyframe byte offsets, flags (locked_until, gap, offloaded)}`, about 64 B. 24 h = 21,600 segments ≈ 1.4 MB.
- **The manifest on eMMC is the truth:** every change (segment closed, reclaimed, locked) is appended immediately, then applied in RAM. **The RAM index is a mirror**, loaded once at boot. The manifest is compacted occasionally (new file + atomic rename).
- **Queries never scan the eMMC:** sessions and segments come from RAM (binary search, ~15 steps for 24 h); fetch reads only the needed files.

### 5.4 Ring and locks

- **The ring is ordinary segment files on ext4 plus a delete rule**, not a special circular area. Large sequential writes and whole-file deletes suit the eMMC's own wear levelling (AC §5).
- **Before each new segment:** make room for `expected_segment_bytes` (bitrate × 4 s × 1.5) + `reserve_bytes`. Free = min(filesystem free, `ring_max_bytes` − used). A small `ring_max_bytes` (~500 MB) makes the ring wrap within a test run.
- **Victim order:**
  1. oldest **closed** session, deleted **whole** (its unlocked segments)
  2. oldest unlocked segment of the **running** session
  3. none -> `storage_full` event (unreachable while the lock cap holds)
- **Delete order:** manifest "deleted" record + fsync -> unlink file -> update RAM.
- **Locks:** whole segments, with reason and TTL, recorded in the manifest. A lock can cover a **post-event window**: the past part locks immediately, the future part is applied to segments as they close. A lock is **refused and reported** if locked bytes would exceed `lock_cap` (~20 % of the ring), so the ring can always free space and recording never stops because of locks. Expired locks become reclaimable.
- **Truncated sessions** stay listed with their available ranges. A session disappears only when its last segment is gone.

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

- The eMMC keeps everything. At boot: load the manifest into RAM; check only the **last session's directory** against it:
  - an unfinished segment is kept up to its last flushed keyframe
  - a file without a record is indexed from its SEIs
  - a "deleted" record whose file still exists -> the file is removed
- The interrupted session is marked `interrupted`. The node reports a `rebooted` event. **No automatic re-arm**: the controller decides (a node re-arming on its own could diverge from its pack).

### 5.7 eMMC layout

- **Trial** (boots from SD): `mmcblk0p1` = `/rec`, whole eMMC (~29.6 GB). The existing p1/p2 are wiped (confirm with Matthew first).
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

Full spec: `docs/icd.md` (**TBD**), machine-checkable: CDDL schema per message + state machine + conformance tests.

- **Transport (trial): HTTP** on `libmicrohttpd`, versioned (`/v1/…`, an `icd` version field in every message; minor = additive only).
- **Encoding: CBOR** (RFC 8949), `Content-Type: application/cbor`. **The node speaks CBOR only.**
- **Schema: CDDL** (RFC 8610), one `.cddl` file per ICD version, in the repo. It is the contract.
- **Code: zcbor** generates the C encode/decode functions (with validation) from the CDDL. The generated code and zcbor's small runtime are vendored into the repo. The same library runs on the Linux node and on Zephyr (zcbor is a Zephyr module).
- **Debugging: JSON on the laptop only.** CBOR maps 1:1 to JSON, so a laptop tool shows responses as JSON and turns JSON input into CBOR (`curl … | cbor2json`, or an `icdctl` CLI). No JSON code on the node.
- **Fetch:** HTTP with range requests, node -> PC directly; the controller never carries video. Segment bytes are raw H.265, not CBOR.
- **Events** (node -> controller): Server-Sent Events carrying CBOR (**TBD**).
- **Product direction:** CoAP instead of HTTP for control and events, because the controller is a Zephyr MCU; same CBOR messages, only the transport changes. Fetch stays HTTP.

**Encoding decision: CBOR + CDDL + zcbor.**
- **Chosen:** an IETF standard; interoperable across the Linux node and the Zephyr MCU (controller, supervisor); native in Zephyr (MCUmgr, MCUboot use it); pairs with CoAP; debuggable as JSON; self-describing (readable without the schema).
- **Considered, protobuf (nanopb):** equally industry-normal for small firmware, with generated code and clear field-number evolution. Not self-describing, and less aligned with the Zephyr/CoAP direction.
- **Rejected:** JSON on the node (larger, slower parsing, a second code path to test); MessagePack (no standard schema, weak for conformance); FlatBuffers (overkill for kbps control traffic); raw C structs/TLV (unportable: endianness, padding, brittle versioning).
- Neither CBOR nor protobuf libraries are in the TI SDK; either way the library is vendored, so the SDK did not decide it.

| Group | Endpoints / content |
|---|---|
| Identity | node id, versions, capabilities |
| Configure | fps (sets the trigger period), exposure, gain, WB, bitrate, trigger source |
| Session | arm `{session, T0, period}`, disarm, start/stop |
| Storage | `GET /v1/sessions`, `GET /v1/segments`, `GET /v1/fetch`, `POST /v1/lock`, `POST /v1/unlock` |
| Time | PHC time, PTP offset, last index |
| Health | ring used/free/depth in hours, locked bytes, eMMC life, drops/gaps/spurious, temperature, link |
| Events | drop, trigger lost, storage nearly full, lock refused, rebooted |

**Failure behaviour:**
- Controller lost: keep recording, queue events.
- Trigger lost: flag segments `untriggered`; invent no indices.
- Storage full: §5.4.

A software **node simulator** serves the same interface, so the conformance suite runs without hardware.

---

## 7. Controller and supervisor (design only, not on the bench)

- **Controller (B3, nRF54L15 + LAN9668, Zephyr):** fleet registry, session plans `{session, T0, period}`, gPTP time master via the LAN9668, command fan-out, health aggregation. Never in the video path.
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
- **Now (bench):** cross-compile with the installed SDK (`linux-devkit/environment-setup-aarch64-oe-linux`) and copy to the board running the 11.0 image (user space compatible, §1.2).
- **Build system: CMake.**
  - One code base, two builds: the laptop (unit tests for recorder, matching, CBOR) and the board (cross-compiled). A small toolchain file `cmake/j722s-sdk.cmake` points at the SDK compiler and sysroot.
  - `pkg_check_modules()` resolves GStreamer and libmicrohttpd against the SDK sysroot.
  - `add_custom_command()` regenerates the zcbor C code when the `.cddl` changes.
  - `ctest` for tests; `compile_commands.json` for tooling; the standard Yocto `cmake` class for the product image.
  - Considered: plain Make (simpler and transparent, but two targets + pkg-config + code generation + tests get messy); Meson (GStreamer's own build system, attractive for our future elements, but not installed and CMake builds GStreamer plugins too).
- **Kernel work** (overlay, driver) needs the matching kernel: install the SDK 6.12.57 kernel + modules + DTBs, or flash a fresh 11.02 image to a spare SD.
- **Update/rollback:** A/B rootfs on eMMC (§5.7), boot counting in U-Boot (e.g. RAUC). **TBD**.
- **Secure boot/identity:** board is HS-FS; product uses HS-SE with our keys; per-device key in OP-TEE / eMMC RPMB. **TBD**.

---

## 10. Test and measurement

### 10.1 Measurements on this node

Full list with commands: `~/logs/0004-measurements-to-do.txt`.

| # | What | Needs |
|---|---|---|
| M1/M2 | eMMC read speed, identity, wear | nothing (read-only) |
| M3 | eMMC write speed (need 0.6-1.2 MB/s) | `/rec` partition |
| M4 | Trigger interval jitter (CPTS timestamps; later a logic analyzer on J1 pin 18) | `trigd` |
| M5 | PTP offset to the laptop | `ptp4l` |
| M6 | Encoder bitrate and CPU @30/@60 | encode pipeline |
| M7 | Frames vs drops, 30+ min, with injected drops | test source + matching |
| M8 | Ring reclaim and lock vs reclaim (small ring, wraps in ~13 min) | recorder |
| M9 | "Last 90 s" query time with a full manifest | recorder + fetch |
| M10 | Recovery: power pull, Ethernet unplug, process kill | recorder + nodeagent |

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

| # | Component | Camera needed? |
|---|---|---|
| A1 | Encode pipeline (test video -> Wave5) | no |
| A2 | `trigd`: CPTS edges + hardware timestamps | no |
| A3 | Trigger-driven test source | no |
| A4 | SEI per frame | no |
| A5 | Recorder: segments, manifest, ring, locks | no |
| A6 | Query/fetch | no |
| A7 | `nodeagent` control interface | no |
| A8 | Matching + drop detection | no |
| A9 | Node simulator + conformance tests | no |
| A10 | Soak and fault tests | no |
| A11 | PTP with the laptop | no |
| B1-B5 | Sensor driver, overlay, ISP path, real binding, strobe measurement | yes |

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
| 3 **Kept, verified** | No supervisor MCU on the standard tier, because TI has 1588 built in (AC §3) | Kept. The J722S CPTS has 2 periodic outputs and 4 timestamp inputs, and the EVM already loops GENF1 into HW3_TS_PUSH: sufficient to generate and timestamp the trigger in hardware (§4.1). | Depends on routing a CPTS output to the camera pin (risk 2, §11.3). | No second codebase or MCU cost on the standard tier. |
