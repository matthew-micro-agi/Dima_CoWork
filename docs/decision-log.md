# Decision Log

Choices, alternatives, reasons and dead ends (brief 5.3). The architecture (`architecture.md`) says **what** we build; this log says **why**, and what we looked at and did not choose.

Format per entry: decision, why, alternatives considered, consequences / future extensions. Dates are 2026-11-12 unless noted. Measurement IDs (P-x, M-x) refer to `architecture.md` §10 and the measurement log.

| ID | Decision | Status |
|---|---|---|
| DL-01 | Time sync: PTP (gPTP, `ptp4l`) between nodes + wired edge within a pack | decided |
| DL-02 | Language for on-node code: C | decided |
| DL-03 | Control-interface encoding: CBOR + CDDL + zcbor | decided |
| DL-04 | Control-interface transport: HTTP now, CoAP later; JSON only on the laptop | decided |
| DL-05 | Build system: CMake | decided |
| DL-06 | Segment index: append-only manifest + RAM mirror; no database | decided |
| DL-07 | Segment length: 4 s | decided |
| DL-08 | Media framework: GStreamer, thin now, own elements later | decided |
| DL-09 | Trigger edge timestamping route: device-tree overlay (timesync router) | decided, not deployed (gap G1) |
| DL-10 | Trial camera rule: existing driver first, trigger support decides | decided |

---

## DL-01 Time sync: PTP (gPTP)

**Decision.** Nodes synchronise their hardware clocks with **PTP, gPTP profile (IEEE 802.1AS), using `ptp4l` / `phc2sys` (linuxptp)**. Within a pack, a **wired edge** triggers all sensors; between packs, each pack generates its edge from PTP time (as in the Architecture Context, AC §4).

**Why.**
- Uses the Ethernet backbone we have anyway; no extra cabling between packs.
- The industry standard for "many devices, one Ethernet, µs-level sync" (TSN, automotive, broadcast, telecom).
- Our hardware supports it: CPSW has hardware timestamping. **Measured (P-3): 30 ns RMS, 85 ns max offset** board vs laptop over a direct cable.
- Matches the controller design (LAN9668 TSN switch, Zephyr gPTP).
- `ptp4l` is the reference Linux implementation; a few hundred nodes per network is ordinary for it.

**Alternatives considered.**

| Alternative | Typical accuracy | Why not (now) |
|---|---|---|
| NTP with hardware timestamps (`chrony`) | ~1-10 µs on a quiet LAN | no switch assistance, worse under video load; not used in TSN. Possible fallback. |
| Wired sync line across packs (RS-485/LVDS) | ns | extra cabling, doesn't scale "without limit", no time of day. Kept **within** a pack. |
| IRIG-B + PPS | ~µs | separate cable, dated tooling, no advantage over PTP |
| GNSS per node | 20-50 ns to UTC | needs sky view; indoor head-mounted device |
| White Rabbit | sub-ns | special switches/fibre; accuracy we don't need |
| SyncE | frequency only | a supplement to PTP, not a replacement |
| EtherCAT distributed clocks | ~100 ns | its own fieldbus; doesn't carry our video/control |
| Radio (Nordic 2.4 GHz, UWB, Wi-Fi timing) | µs or better (varies) | unproven for us; relevant only to the lite tier's cable-free option (AC §12 q5) |

**Requirements at scale (100 nodes).**
- **Time-aware switches** in every hop (transparent/boundary clocks or 802.1AS bridges); plain switches add µs-ms of queuing noise under video load.
- **One gPTP profile** on all nodes, part of our image. (The P-3 bench check used the default 1588 settings, not yet gPTP.)
- **A real grandmaster** (controller/switch with a good oscillator, UTC from GNSS or NTP) with failover and holdover.
- **Per-node monitoring:** PTP state and offset in health (already in the ICD); alarms; frames recorded while unsynced are flagged.
- **NIC/driver qualification** per hardware type (offset/jitter test like P-3). Lesson from the bench: the laptop's Intel I219 clock ran ~1600 ppm off and only works as grandmaster with `phc2sys`.
- Matched cable lengths within a pack; absolute accuracy calibrated once per hardware type (PPS + scope).

**Future extensions (compatible with this decision, not done now).**
- *Security:* closed network and switch port roles (accept announcements only from the grandmaster's port; nodes slave-only) as the baseline; PTP authentication TLV (IEEE 1588-2019 security annex, supported by linuxptp) or MACsec if the threat model requires it (check support in Zephyr and the switch first); NTS-secured NTP for the grandmaster's UTC source; alarms on path-delay jumps (delay attacks can be detected, not prevented by crypto).
- *Low power:* Single Pair Ethernet (10BASE-T1L) for sync/control links; Energy-Efficient Ethernet off on PTP links (it adds variable latency); TCXO for holdover; radio sync as the lite-tier experiment. Time sync itself is negligible next to the ~1-2 W of a recording node *(estimate)*; the physical link dominates.

---

## DL-02 Language for on-node code: C

**Decision.** All on-node code in C: `trigd`, `nodecap` + `recorder` (one process), `nodeagent`.

**Why.** Our code is on the data path; GStreamer's native API is C; the SDK cross-toolchain is ready; small footprint, which also fits the lite tier (RV1106: one A7 core, 128-256 MB RAM). Libraries: `libmicrohttpd` 12.62.1 is in the SDK sysroot and on the board image.

**Alternatives.**
- **Rust:** likely the right long-term choice (memory safety, official GStreamer plugins via `gst-plugins-rs`), but its cross-toolchain setup is too long for the prototype. **Product option.**
- **Python** (on the node): rejected. Slower, less control, much more CPU and memory than a firmware system should spend.

Laptop test tools are not firmware; their language is decided separately.

---

## DL-03 Control-interface encoding: CBOR + CDDL + zcbor

**Decision.** Messages are **CBOR** (RFC 8949), the schema is **CDDL** (RFC 8610, one file per ICD version, the contract), C code is generated by **zcbor**. The node speaks CBOR only.

**Why.** IETF standard; interoperable across the Linux node and the Zephyr MCU (controller, supervisor); native in Zephyr (MCUmgr, MCUboot); pairs with CoAP; debuggable as JSON (1:1 mapping); self-describing.

**Alternatives.**
- **Protobuf (nanopb):** equally industry-normal for small firmware, generated code, clear field-number evolution. Not self-describing; less aligned with the Zephyr/CoAP direction.
- **JSON on the node:** rejected. Larger, slower parsing, a second code path to test.
- **MessagePack:** rejected. No standard schema, weak for conformance.
- **FlatBuffers:** rejected. Overkill for kbps control traffic.
- **Raw C structs / TLV:** rejected. Unportable (endianness, padding), brittle versioning.

Neither CBOR nor protobuf libraries are in the TI SDK; either way the library is vendored, so the SDK did not decide it.

**Dead end.** zcbor 0.9.1 declares `cbor2 >= 5.4.2` without an upper bound; pip installed cbor2 6.2.1, which removed `CBORDecodeValueError`, so zcbor failed on import. Fixed by pinning `cbor2 < 6` (5.9.0) in `tools-requirements.txt`.

---

## DL-04 Control-interface transport: HTTP now, CoAP later

**Decision.** Trial: **HTTP** on `libmicrohttpd`, versioned (`/v1/…`). Segment fetch: HTTP range requests, node -> PC directly. Events: Server-Sent Events carrying CBOR (**TBD**). Product direction: **CoAP** for control and events (same CBOR messages). **JSON exists only on the laptop** as a debug view (`cbor2json`, `icdctl`).

**Why.** HTTP is testable with standard tools, and `libmicrohttpd` is already on the board. CoAP fits a Zephyr MCU controller. Keeping JSON off the node keeps the firmware lean (one encode/decode path).

**Alternatives.**
- Node serving both CBOR and JSON: rejected (a JSON library and a second code path on the node).
- gRPC: rejected (no gRPC on Zephyr; heavy on an MCU).
- MQTT: rejected for now (needs a broker; the controller is too small to host one).

---

## DL-05 Build system: CMake

**Decision.** CMake, with a toolchain file `cmake/j722s-sdk.cmake` pointing at the SDK compiler and sysroot.

**Why.** One code base, two builds (laptop for unit tests, board cross-compiled); `pkg_check_modules()` against the sysroot; `add_custom_command()` regenerates zcbor code when the `.cddl` changes; `ctest`; `compile_commands.json`; the standard Yocto `cmake` class later. Already installed.

**Alternatives.**
- **Plain Make:** simpler and transparent, but two targets + pkg-config + code generation + tests get messy.
- **Meson:** GStreamer's own build system, attractive for our future elements, but not installed, and CMake builds GStreamer plugins too.

---

## DL-06 Segment index: append-only manifest, no database

**Decision.** `manifest.bin` on eMMC, append-only, is the truth; `recorder` keeps a RAM mirror and is its **only owner**; `nodeagent` queries `recorder` over the local socket.

**Why.** Writes are simple by construction (one append per 4 s segment, deletes recorded before unlinking), so recovery checks only the last few records. Queries are fixed and few (session, index range, time range): binary search is optimal.

**Alternative: a lightweight database (SQLite), rejected.**
- No multi-record transactions to protect.
- Multi-process access is covered by the single owner.
- Costs an embedded node should not spend: several times more eMMC writes (write-ahead log, page writes, checkpoints) on a 24/7 recorder, a library of several hundred KB plus SQL parsing, checkpoint timing we don't control, one more dependency.
- Reconsider only for rich metadata with many query types, or several independent writers.

---

## DL-07 Segment length: 4 s

**Decision.** 4 s segments; keyframe every 1 s (**TBD**: 1 or 2 s).

**Why.** Power of 2; whole frame counts (120 @30 fps, 240 @60 fps), so segment boundaries fall on trigger indices; ~2.5 MB @1080p30, ~5 MB @1080p60 (under 10 MB); within the AC §5 range of 1-10 s.

**Alternatives.** 5 s (first proposal, no strong reason); 10 s (fewer files, but coarser locks and staler fetch). Shorter segments' advantages (fresher fetch, less loss on crash) are covered separately by per-keyframe flushing and fetching from the open segment.

---

## DL-08 Media framework: GStreamer

**Decision.** GStreamer for capture, ISP and encode. Now: a thin pipeline with our code at `appsrc` / `appsink`. Later: the same logic wrapped as our own elements (`trigsrc`, `trigsei`, `segmentsink`) for one portable pipeline.

**Why.** The TI ISP (VPAC) is practically reachable only through TI's `tiovx*` GStreamer elements; encoder, capture and test source come for free; industry standard and portable (on Rockchip only the encoder element changes). Logic written as plain functions so it can be wrapped without rewriting.

**Alternative.** Plain V4L2 in C: more control and less overhead for the encoder, but doesn't solve the ISP.

**Dead ends (P-1).** `videotestsrc` renders in software (~10-40 fps at 100 % of a core), so the first encoder run measured the source; rate control is off by default (`frame_level_rate_control_enable=0`); `rawvideoparse` -> encoder fails negotiation unless `colorimetry=bt709` is set; `identity eos-after` is not exact.

---

## DL-09 Trigger edge timestamping: device-tree overlay

**Decision.** Route CPTS GENF1 into HW3_TS_PUSH with a **device-tree overlay** on the unmodified base tree: it adds the timesync router (`reg-mux`, `mux-reg-masks-state = <0x34 0x0001ffff 0x10011>`), without `ti,pps` so `trigd` owns GENF1. Source: `dts/k3-j722s-evm-fwtrial-cpts-loop.dtso`. **Not deployed yet** (gap G1, `architecture.md` §11.6); until then `trigd` runs in computed-time mode.

**How we got here.** P-2: the periodic output ran but no timestamps arrived; the board's 11.0 device tree has no timesync router and no `ti,pps` (the loop exists only in the 11.02 SDK tree). The board kernel supports the overlay (`CONFIG_MUX_MMIO=y`, TI's `mux-reg-masks-state` extension present).

**Alternatives.**
- Booting a **decompiled live tree** with edits: rejected. It bakes in U-Boot's boot-time fixups, loses labels, and replaces the reference tree wholesale.
- The **11.02 kernel + device tree**: rejected for now. Larger change; revisit for kernel/driver work.
- A **physical wire** GENF pin -> timestamp pin: rejected. Board modification, needs pin-mux overlays anyway.

---

## DL-10 Trial camera rule

**Decision.** For the trial, prefer a camera **with an existing driver in the TI SDK**; among those, **external-trigger support** decides. Without trigger support, the trigger chain is proven to the connector pin with the test source and the gap is stated. The **AR0235 driver and ISP tuning are ours** (AC §7) and a later milestone. The trial starts without camera input (test source).

**Why.** An existing driver turns days of driver work into an overlay and a reboot; the brief rewards cutting scope sensibly. The hardware trigger (AC §4, firm) is the property that matters most, so it decides between supported cameras.
