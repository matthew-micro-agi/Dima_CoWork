# Decision Log

Choices, reasons, alternatives and dead ends. Readable on its own.

**Context.** A recording node for hardware-synchronised cameras: each camera's node encodes H.265 in hardware and records to its own eMMC as a ring of segment files. A hardware trigger starts every frame; each frame carries the index of its trigger edge. A controller runs many nodes over Ethernet. Bench: TI J722S evaluation board (EVM) with TI's Linux SDK image. Product tiers later: TI-based standard, Rockchip-based "lite" with a supervisor microcontroller. "Original description" = the hiring team's system description.

All dates 2026-11-12.

| ID | Decision |
|---|---|
| DL-01 | Time sync: PTP (gPTP) between nodes, wired trigger edge within a pack |
| DL-02 | Code on the node: C |
| DL-03 | Control-interface encoding: CBOR + CDDL + zcbor |
| DL-04 | Control-interface transport: HTTP now, CoAP later; JSON only on the laptop |
| DL-05 | Build system: CMake |
| DL-06 | Segment index: append-only manifest, no database |
| DL-07 | Segment length: 4 s |
| DL-08 | Media framework: GStreamer |
| DL-09 | Timestamping our own trigger edges: device-tree overlay |
| DL-10 | Trial camera: existing driver first, trigger support decides |
| DL-11 | Trigger daemon -> consumers: datagram socket, versioned struct, subscription |
| DL-12 | Session plan delivery and start time |
| DL-13 | Clock not verified as synchronised |
| DL-14 | Trigger output after a trigger-daemon crash |
| DL-15 | Frame timestamp (PTS) = edge time - T0 |
| DL-16 | SEI insertion: our own C code now, GStreamer element later |
| DL-17 | Frame index drawn into test frames |
| DL-18 | Segment cut: first keyframe at or after the boundary |
| DL-19 | Manifest: fixed 128-byte records with checksum |
| DL-20 | Session number: made by the single plan maker, never by a node |
| DL-21 | Recorder v1: full scope |
| DL-22 | Control interface reads the recordings index read-only |
| DL-23 | Session start: nodeagent starts trigd + nodecap per session |
| DL-24 | Deploy: tarball + install script, release directories, systemd |
| DL-25 | PTP on the node as a systemd service |
| DL-26 | Camera emulator: separate test process with a V4L2-like buffer protocol |
| DL-27 | Frame-to-edge binding by time, shared by all camera sources |

---

## DL-01 Time sync: PTP (gPTP)

- **Decision:** nodes sync their hardware clocks with PTP (IEEE 1588), gPTP profile (IEEE 802.1AS), using linuxptp (`ptp4l`, `phc2sys`). Within a pack (~10 cameras) one wired edge triggers all sensors; each pack generates its edge from PTP time. As in the original description.
- **Why:** uses the existing Ethernet; industry standard for µs-level sync over Ethernet (TSN, automotive, broadcast, telecom); board timestamps in hardware (**bench: 30 ns RMS, 85 ns max** board vs laptop); matches the planned controller (LAN9668 TSN switch, Zephyr gPTP); `ptp4l` handles hundreds of nodes.
- **Alternatives:**
  - NTP with hardware timestamps (chrony), ~1-10 µs: no switch support, worse under video load. Possible fallback.
  - Wired sync line across packs, ns: extra cabling, no time of day. Kept within a pack.
  - IRIG-B + PPS, ~µs: separate cable, no advantage.
  - GNSS per node, 20-50 ns: needs sky view; device is indoors.
  - White Rabbit, sub-ns: special switches, overkill.
  - Synchronous Ethernet: frequency only, a PTP supplement.
  - EtherCAT clocks, ~100 ns: separate fieldbus.
  - Radio time transfer (Nordic, UWB, Wi-Fi): unproven; only for a cable-free lite tier.
- **At ~100 nodes:** time-aware switches in every hop; one gPTP configuration in the image (bench check used linuxptp defaults); a real grandmaster with failover and holdover; PTP state and offset in node health; qualify each NIC/driver (the laptop's Intel I219 clock ran ~1600 ppm off and needs `phc2sys`); calibrate absolute accuracy once per hardware type (PTP can't see asymmetric delays).
- **Later, security:** closed network + switch port roles + nodes slave-only as baseline; PTP authentication or MACsec if needed (check Zephyr/switch support); NTS for the grandmaster's UTC; alarms on path-delay jumps (delay attacks: detectable, not preventable).
- **Later, low power:** Single Pair Ethernet (10BASE-T1L) for sync/control links; Energy-Efficient Ethernet off on PTP links; TCXO for holdover. Sync cost is negligible next to a recording node (~1-2 W, estimate).

## DL-02 Code on the node: C

- **Decision:** all on-node code in C (trigger daemon, capture/recorder, control server).
- **Why:** data path; GStreamer's API is C; SDK cross-compiler ready; small footprint, fits the lite tier (one Cortex-A7, 128-256 MB). `libmicrohttpd` already on board and in the SDK.
- **Alternatives:** Rust: likely long-term choice (safety, official GStreamer plugins), cross-toolchain setup too long for a prototype; product option. Python: rejected (slow, heavy, less control).
- Laptop-only tools are not firmware; their language is separate.
- Stream checker (`sei-dump`, reads the per-frame SEI on the laptop): C, sharing the SEI code with the node.

## DL-03 Control-interface encoding: CBOR + CDDL + zcbor

- **Decision:** messages in CBOR (RFC 8949); schema in CDDL (RFC 8610), one file per interface version, the contract; C code generated by zcbor. The node speaks CBOR only.
- **Why:** IETF standard; works on the Linux node and Zephyr microcontrollers (built into Zephyr); pairs with CoAP; maps 1:1 to JSON for humans; self-describing.
- **Alternatives:** protobuf/nanopb: equally common, not self-describing, less Zephyr/CoAP-aligned. Rejected: JSON on the node (size, second code path), MessagePack (no schema), FlatBuffers (overkill), raw C structs (unportable between independent implementations).
- Neither library is in the TI SDK; bundled either way.
- **Dead end:** zcbor 0.9.1 breaks with cbor2 6.x (removed class); pinned cbor2 5.9.0 in `tools-requirements.txt`.

## DL-04 Control-interface transport: HTTP now, CoAP later

- **Decision:** HTTP (`libmicrohttpd`), versioned paths (`/v1/…`). PC fetches recordings directly from the node (HTTP range requests). Events: Server-Sent Events with CBOR (to confirm). Product: CoAP, same messages. JSON only on the laptop, via a converter.
- **Why:** HTTP is easy to test and already on the board; CoAP suits a microcontroller controller; no JSON on the node keeps it small.
- **Alternatives:** node serving CBOR and JSON (extra code); gRPC (not on Zephyr); MQTT (needs a broker the controller can't host).
- **Deviation from the original description:** there, the controller forwards video to a PC. A microcontroller can't forward ~100 Mbps, so the PC fetches directly; the controller only orchestrates.

## DL-05 Build system: CMake

- **Decision:** CMake; toolchain file `cmake/j722s-sdk.cmake` points at the SDK compiler and sysroot.
- **Why:** one code base, laptop build (unit tests) + board cross-build; pkg-config against the sysroot; regenerates zcbor code from the CDDL; `ctest`; Yocto support later; already installed.
- **Alternatives:** plain Make (messy with two targets, codegen, tests); Meson (GStreamer's own, not installed; CMake builds plugins too).

## DL-06 Segment index: append-only manifest, no database

- **Decision:** `manifest.bin` on eMMC, one record per segment, append-only, the source of truth. The recorder loads it into memory and is its only owner; the control server asks the recorder over a local socket.
- **Why:** one append per 4 s; deletions recorded before files are removed, so a crash needs only the last records checked; few fixed queries (session, index range, time range) → binary search, ~15 steps for 24 h.
- **Alternative, rejected:** SQLite. No multi-record transactions to protect; single owner covers multi-process access; costs several times more eMMC writes, a large library, uncontrolled checkpoints, one more dependency. Reconsider only for rich metadata or several writers.

## DL-07 Segment length: 4 s

- **Decision:** 4 s segments; keyframe every 1 s (1 or 2 s to confirm); each segment starts with a keyframe and decodes alone.
- **Why:** power of 2; whole frame counts (120 @30 fps, 240 @60 fps) so boundaries fall on trigger indices; ~2.5 MB @1080p30, ~5 MB @1080p60; within the original 1-10 s range.
- **Alternatives:** 5 s (no strong reason); 10 s (coarser locks, older fetch data). Shorter segments' benefits come from per-keyframe flushing and fetching the open segment.

## DL-08 Media framework: GStreamer

- **Decision:** GStreamer for capture, ISP, encode. Now: thin pipeline, our code at `appsrc`/`appsink`. Later: our logic as GStreamer elements for one portable pipeline.
- **Why:** TI's ISP is only practical via TI's GStreamer elements; encoder, capture, test sources ready-made; portable (on Rockchip only the encoder element changes). Logic written as plain functions so wrapping needs no rewrite.
- **Alternative:** plain V4L2 in C: more control, but no ISP access.
- **Dead ends (encoder bench test):** `videotestsrc` draws in software (10-40 fps, one full core), so the first test measured the source; encoder rate control is off by default (bitrate ignored); raw sources need `colorimetry=bt709` or the encoder refuses them; `identity eos-after` isn't exact.

## DL-09 Timestamping our own trigger edges: device-tree overlay

- **Decision:** route the PTP periodic output 1 into timestamp input 3 through the SoC's timesync router, via a device-tree overlay on TI's unmodified tree (`dts/k3-j722s-evm-fwtrial-cpts-loop.dtso`). **Not deployed.** Until then edge times are computed, not measured (DL-13).
- **Why:** on the bench the output ran but no timestamps arrived; the board's 11.0 device tree has no router configured (only the 11.02 SDK does); the board kernel supports the overlay.
- **Alternatives, rejected:** boot an edited decompiled live tree (bakes in bootloader fixups, replaces TI's tree); 11.02 kernel + tree (much larger change, revisit for driver work); a wire between pins (board modification).
- **Later:** deploy it and measure edge times; that proves each edge fired and gives jitter numbers.

## DL-10 Trial camera

- **Decision:** prefer a camera with a driver in the TI SDK; among those, external-trigger support decides. Without trigger support, prove the trigger chain to the connector pin and state the gap. Product sensor (AR0235) driver and tuning: ours, later. Trial starts with a trigger-driven test source.
- **Why:** an existing driver turns days of work into configuration; the hardware trigger is what the product depends on.

## DL-11 Trigger daemon -> consumers

- **Decision:** `trigd` sends one message per edge over a Unix datagram socket: versioned C struct {version, session, index, time ns, flags}. Consumers subscribe; `trigd` sends to every subscriber.
- **Why socket:** 30-60 messages/s is trivial; no library; replaceable later (e.g. shared memory) without changing the message.
- **Why C struct here, CBOR on the control interface:** internal IPC between our own binaries built together vs a contract between independent implementations. Version field allows changes.
- **Why subscription now:** several cameras per node (one capture process each; J722S has four camera connectors), IMU recording on the same edges, diagnostics without stopping capture. A few dozen lines now vs changing `trigd` later.
- **Later:** shared memory if rates grow; CBOR if consumers are built separately.

## DL-12 Session plan delivery and start time

- **Decision:** session plan = {session, T0 (PTP time of edge 0), period}. Product: control server -> `trigd` over a local Unix control socket. First version: command-line arguments, same structure.
- **T0:** always in the future, so command arrival time (varies by ms) doesn't matter; every node fires edge 0 at T0. Default: next whole PTP second + 1 s; offset configurable; explicit T0 possible (the controller's way).
- **Why:** same plan structure, so first-version code is product code; whole seconds read easily; 1 s leaves time to arm.

## DL-13 Clock not verified as synchronised

- **Problem:** edge times are only comparable across nodes if the PTP clock is synchronised. Unsynchronised: each node stays self-consistent, but the same index means different instants on different nodes, unnoticed. If PTP jumps the clock mid-session, the schedule jumps too.
- **Now:** arm anyway; every edge carries a "time not verified" flag. The first version can't check PTP yet, so the flag is set on every edge.
- **Later (product):** ask `ptp4l` (its management socket) and clear the flag only while locked; refuse to start a session unless locked; flag edges and raise an event on lock loss or a clock jump; configure `ptp4l` never to jump the clock after first lock.
- The flag is in the message from the start, so these changes touch only `trigd`.

## DL-14 Trigger output after a trigger-daemon crash

- **Problem:** a crash or `SIGKILL` skips the disarm; the hardware keeps firing edges. Linux can't report whether an output is running.
- **Now (implemented with DL-24):** systemd `ExecStopPost=` -> `trigd --disarm-only` runs after every stop of the node service, including a crash or `SIGKILL`; `Restart=on-failure`. `trigd` also still stops the output at every start. Unannounced frames are flagged as spurious by the capture process.
- **Remaining window:** `trigd` crashing while `nodeagent` keeps running: `nodeagent` sees the child die and stops the session; the output is disarmed at the next stop or `trigd` start.
- **Not planned:** crash marker file; silence detection in consumers (comes with the capture process); disarm from a crash signal handler (useless against `SIGKILL`).

## DL-15 Frame timestamp (PTS) = edge time - T0

- **Decision:** each frame's presentation timestamp (PTS) in the pipeline = its trigger edge time minus the session start T0. Index recovered as `round(PTS / period)`.
- **Why:** the same code path as with a real camera (camera frames carry a capture time, matched to edges by time); the true edge time also goes into the SEI. Encoder passes timestamps through V4L2 at µs resolution, far finer than the 16-33 ms period.
- **Alternative:** PTS = `index × period`: exact grid, no rounding, but a test-only path.
- Identical today (edges computed exactly on the grid); they differ once edge times are measured or a camera is used.

## DL-16 SEI insertion: our own C code now, GStreamer element later

- **Decision:** our code adds one SEI per frame (H.265 "user data unregistered", fixed identifier + version + {session, index, edge time, flags}) to the encoded stream at the `appsink`.
- **Why:** pure byte handling, unit-testable on the laptop without GStreamer, easy to debug; doesn't depend on whether GStreamer 1.22's encoder and parser carry SEI metadata through (unverified).
- **Alternative:** GStreamer's SEI metadata on input buffers.
- **Later:** the same functions become the body of our own GStreamer element (DL-08); only the insertion point moves, parser and checker stay.

## DL-17 Frame index drawn into test frames

- **Decision:** the test source draws each frame's index as a row of large black/white squares (binary); the laptop checker decodes the video, reads them and compares with the SEI index.
- **Why:** proves each SEI sits on the right frame. A continuity check alone misses an off-by-one shift (every frame carrying its neighbour's SEI is still continuous).
- **Limit:** test source only; with a camera the equivalent is a visible counter or LED in the scene.
- **Cost:** ~50 lines each side; large high-contrast squares survive compression.

## DL-18 Segment cut: first keyframe at or after the boundary

- **Decision:** a segment ends at the first keyframe at or after its boundary index (multiple of 4 s worth of frames, 120 @30 fps); the next file is named by its real first index.
- **Why:** the encoder counts frames, not trigger indices; after a dropped frame the boundary index may not be a keyframe. Cutting only at keyframes keeps every segment decodable on its own. Segments are 4 s, up to +1 s after a drop.
- **Alternative:** force a keyframe at each boundary index: exact boundaries, relies on the encoder honouring the request.

## DL-19 Manifest: fixed 128-byte records with checksum

- **Decision:** every manifest record (session start/end, segment closed, segment deleted, lock, unlock) is 128 bytes: header (magic, version, type, sequence number, session), type-specific fields, CRC-32 at the end.
- **Why:** simple to write, read and seek; a record half-written at power loss fails its CRC and is dropped (and cut off) at the next start; room for 8 keyframe positions per segment (4 s at 0.5-1 s keyframe interval).
- **Alternative:** variable-length records: smaller, more parsing code and harder torn-write detection.

## DL-20 Session number: made by the single plan maker, never by a node

- **Decision:** the session number is part of the session plan {session, T0, period} and comes from the one place that makes the plan: the controller in the product, the laptop (test script or operator) in the trial. Every node of a pack gets the same number. `trigd` requires `--session` (no default); the recorder refuses a number already on disk.
- **Why:** (session, index) must mean the same instant on every node; a number made per node (or a default like 1) breaks that or silently mixes two runs in one directory.
- **Alternatives:** random number per node (breaks cross-node identity); number from T0 (the board's PTP clock restarts at 0 each boot when unsynchronised, so numbers repeat); counter stored per node (nodes disagree).
- **Later (controller):** random numbers need no stored state on a microcontroller; widen from 32 to 64 bits if sessions become very many (message and SEI version bump). The recorder's refusal stays as the safety net.

## DL-21 Recorder v1: full scope

- **Decision:** v1 = segments, manifest, ring, locks (incl. post-event window and lock cap), recovery after crash or power loss, `rec-tool` (list, verify, lock). Fetch over the network comes with the control interface (`nodeagent`).
- **Why:** the brief's storage questions (ring, locks, crash behaviour) are answered only by the full set; fetch needs the control interface anyway.

## DL-22 Control interface reads the recordings index read-only

- **Decision:** `nodeagent` reads `/rec/manifest.bin` read-only for queries and fetch; lock/unlock go to the running recorder over its control socket, or directly when no session runs. Queries and fetch answer **immediately with the last known good state** (all closed segments); a running session's open segment (at most the last 4 s) is not included yet, and the response says so.
- **Why:** the recorder lives inside `nodecap`, which runs only during a session; the manifest is append-only with a CRC per record, so a concurrent reader sees a complete state (a half-written last record is skipped). No query protocol needed now.
- **Safety:** GET never changes state; inputs validated (strict numbers, CBOR decoded by generated code, body limit); limits on simultaneous fetches, fetch length and connection time; a segment opened for fetch stays readable if the ring deletes it meanwhile.
- **Alternative:** queries over the recorder's socket (strict single owner, includes the open segment) needs a recorder running as its own daemon: bigger restructure.
- **Later:** open segment up to its last flushed keyframe via recorder queries.

## DL-23 Session start: nodeagent starts trigd + nodecap per session

- **Decision:** `nodeagent` is the only long-running process. `POST /v1/arm {session, t0?, fps}` starts `trigd` and `nodecap --rec /rec` as child processes for that session; disarm stops them cleanly; one session at a time; a child that dies stops the session and shows in health.
- **Why:** smallest working path for the trial; the HTTP interface is the same as the product's.
- **Alternative (product):** `trigd` and `nodecap` run permanently and receive session plans over local sockets (DL-12), supervised by systemd (DL-14).

## DL-24 Deploy: tarball + install script, release directories, systemd

- **Decision:** `deploy/make-bundle.sh` cross-builds and packs `fwtrial-<version>-aarch64.tar.gz` (binaries, systemd units, default configs, `install.sh`, `VERSION`). `deploy/deploy.sh <user@node>` is the one command: bundle -> copy -> `install.sh` on the node -> check `GET /v1/identity` reports the new version. On the node: `/opt/fwtrial/releases/<version>/`, symlink `/opt/fwtrial/current` switched atomically, previous release kept, `install.sh --rollback`; units enabled (start at boot); configs in `/etc/fwtrial/` written only if missing; `/rec` never touched. Version = project version + git commit (`-dirty` if uncommitted).
- **Why:** a newcomer goes from checkout to a running service with one command; reboot-safe; release + symlink gives rollback, the same idea as the product's A/B update; reuses CMake's install rules.
- **Alternatives:** `opkg` package (the image has opkg; needs packaging metadata and a feed); Yocto image now (product path, architecture §9; too much for the trial); copying into `/usr/local` (no rollback, hard to clean up).
- **Check scripts:** board runs stop the installed service first and start it again afterwards (they use the same port, sockets and `/rec`).
- **Later:** Yocto recipe in `meta-fwtrial` installing the same units and configs; signed bundles.

## DL-25 PTP on the node as a systemd service

- **Decision:** own unit `fwtrial-ptp4l.service` with own config `/etc/fwtrial/ptp4l.conf`: client only (never becomes master), layer-2 transport, hardware timestamps, `eth0`; enabled at boot, can be disabled on its own. The image's `ptp4l@.service` and `/etc/linuxptp/ptp4l.conf` stay untouched (their default allows the node to become master).
- **Why:** trigger edges are scheduled on PTP time (DL-01); time sync must start with the node, not by hand.
- **Later:** gPTP profile settings, lock state checked by `trigd`/`nodeagent` (DL-13), `phc2sys` if the system clock must follow (lock expiry uses the wall clock).

## DL-26 Camera emulator: separate test process with a V4L2-like buffer protocol

- **Decision:** `cam-emu`, a separate process for testing only (not in the node bundle), plays a sensor wired to the trigger: it subscribes to `trigd` and delivers one frame per edge after a configured latency ± jitter, with its own monotonic timestamp and sequence counter. Its local-socket protocol mirrors V4L2 step by step: format, request N buffers (one shared-memory file, fd handed over), queue / dequeue buffers, stream on / off. With no buffer queued the frame is lost and the sequence still advances, as with a real driver.
- **Faults on request:** drop %, extra (spurious) frames %, latency + jitter, late frames beyond the match window, sequence jump; content: a moving, noisy pattern.
- **Ground truth:** the emulator draws the true edge index into each frame (a special value for spurious frames); `nodecap` must find the index by timing alone; `sei-dump` compares SEI index and drawn index frame by frame.
- **Why:** a real camera is a file descriptor delivering buffers with a timestamp and a sequence counter; an emulator behind the same kind of interface tests the code path a camera will use, including back-pressure (frames lost when all buffers are held), and keeps test code out of the node.
- **Alternatives:** emulator inside `nodecap` (easier, but a path no camera uses); frames through a pipe (copies 3.5 MB per frame, unlike a camera); `vivid` kernel driver (real V4L2, but free-running, can't be triggered); `v4l2loopback` (a real `/dev/videoN`, but an out-of-tree module that must be built for the board kernel, same issue as the sensor driver).
- **`nodecap` sources:** `--src test` (built-in, kept as a second path), `--src emu` (this emulator), later `--src v4l2:/dev/videoN` (camera: same calls as `ioctl`s).

## DL-27 Frame-to-edge binding by time, shared by all camera sources

- **Decision:** for external frame sources (emulator, camera) `nodecap` converts edge times from PTP to monotonic time and binds a frame to the edge whose time + expected latency is closest, within a window. No edge in the window -> spurious frame, not encoded, no invented index; two frames for one edge -> the closer one kept; edge without a frame -> gap; sequence jumps counted next to gaps. Expected latency configured (`--cam-latency-ms`, default 25 ms).
- **Window:** `--cam-window-ms`, default 3 ms; refused at a quarter period or more; warning when a bound frame lies beyond half the window (window too tight for this camera). Rule: several times the measured spread of frame timestamps, and below a quarter period.
- **Why:** the index must come from the trigger, never from counting frames; the same logic serves the emulator now and the camera later.
- **Why not half a period (first version):** a frame late by 3/4 period lands 1/4 period from the next edge; if that edge's own frame is missing, the late frame takes its index. Seen in the emulator stress run (60 fps, 3 % drop/extra/late): 1 frame with a wrong index at +-8.3 ms and at +-5 ms, none at +-3 ms. Below a quarter period minus jitter, every fault the emulator makes is rejected, so the emulator check is strict: any wrong index is a bug.
- **Limit:** timing alone can't be fully safe. A frame landing where a missing frame would have been (sensor glitch, a strongly delayed frame) is indistinguishable by time; the sequence counter doesn't show it either (one lost + one extra cancel out). A smaller window only makes it rarer.
- **Later:**
  - Before a real camera is trusted: the sensor's own frame counter (AR0234 embedded data lines) as a second, independent signal, so each frame carries which trigger made it; timing becomes the cross-check.
  - Real camera bring-up: measure the timestamp spread (fixed exposure, under load), then set the window.
  - Latency measured and tracked automatically (it moves with exposure time if the timestamp marks the end of the frame).
