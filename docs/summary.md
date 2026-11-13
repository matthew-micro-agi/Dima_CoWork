# Summary (30-minute read)

Two-day firmware trial on a TI J722S EVM: one recording node of a hardware-synchronised multi-camera recorder. Numbers: `numbers.md`. Design: `architecture.md`. Reasons and dead ends: `decision-log.md`. Reproduce: `README.md` -> `setup.md`.

## 1. The idea in one chain

One PTP time base on every node -> one plan `{session, T0, P}` to all nodes -> each node's CPTS fires trigger edge k at T0 + k·P in hardware and captures its time -> the edge starts the sensor's exposure -> `nodecap` binds each frame to its edge by time -> every encoded frame carries `{session, k, t_edge}` in an SEI -> same (session, k) on any node = the same instant. Indices come from time, never from counting, so a lost frame shows as a gap instead of shifting everything after it (architecture §1, §4).

## 2. What works (measured on the board)

- **30 minutes at 60 fps, 1920x1200, H.265 on Wave5, recorded to the eMMC with the ring wrapping: 108,001 of 108,001 frames, 0 missing, 0 with a wrong index** (ground truth drawn into each frame by the camera emulator, checked by decoding). Storage stalls of up to 1.2 s did not cost a frame.
- **Load:** our processes use 34.7 % of one A53 core (8.7 % of the four); 62-66 °C.
- **Trigger in hardware:** the CPTS fires every edge on the PTP clock and captures its time (our device-tree overlay); interval = period within the 2 ns resolution when PTP doesn't correct the frequency.
- **Time:** PTP client at boot; 51-52 ns RMS to the bench grandmaster over 30 min.
- **Binding by time:** frames bound to edges by timestamp with a ±3 ms window; under injected faults (drop, extra, late, sequence jumps) every count matched and no frame got a wrong index.
- **Recorder:** 4 s segments, append-only manifest with CRC, ring, locks with a cap, crash recovery; "last 90 s" (~113 MB) fetched in ~1 s with the first byte in ~2 ms while recording and reclaiming.
- **Control interface:** HTTP + CBOR, the contract in CDDL (`icd/fwt-icd-v1.cddl`), C code generated with zcbor; a C client `fwt-rec` (record, status, fetch, check) that is also the start of the controller code.
- **Operations:** one-command deploy with rollback and uninstall, systemd services, a host bundle for operators (no build tools), read-only pre-checks (`fwt-doctor`), end-to-end checks per component on laptop and board.

## 3. What doesn't work, or isn't shown

| Item | State | Consequence |
|---|---|---|
| **Real camera** (G3) | the bench AR0234 module answers on no camera connector after checking mux, enable pins, I/O level, cable, both connectors | everything downstream of the sensor is proven with a camera emulator behind the same kind of interface as V4L2; driver, overlay, ISP and real latency are open |
| **Edges vs PTP frequency correction** (G4) | with PTP locked, the trigger output runs at the crystal rate (57 ppm off PTP time); cause found in the CPTS driver (only the kernel's PPS output gets the correction) | single-node recording, binding and indices unaffected; edges of different nodes would drift apart, so the multi-node claim needs the driver patch (first item of the next steps) |
| **Several nodes** | described, not shown (one board) | the method and error budget are in architecture §4.3, §10.2 |
| **Trigger at the connector pin** | the edge is generated and captured inside the SoC, not yet muxed to J1 pin 18 | needs the pin mux and a logic analyzer |
| **PTP lock check** | every edge is flagged "time not verified" (DL-13) | the flag is honest; the check (ask `ptp4l`) is designed, not built |
| **Recovery under unplug / power pull** (M10) | not run | process kill and recorder crash recovery are tested; unplug and power pull are not |
| **Events, configure endpoint, conformance suite** | designed, not built | ICD v1 covers arm, disarm, health, sessions, segments, fetch, lock |
| **Security** (G5) | the bench is open: root SSH without a password, control interface without authentication, recordings unencrypted, bundles unsigned, debug ports open | bench / closed lab network only; a security track is planned (threat model first) |

## 4. What I skipped, and why

- **Second node, logic analyzer:** scope decision on day 1: one reliable node with numbers beats a broad demo; multi-node stays described.
- **Camera bring-up beyond diagnosis:** blocked by hardware (G3); the camera emulator (`cam-emu`) let the whole path be tested, including the timing logic a camera needs, instead of waiting.
- **Product platform** (Yocto layer, A/B updates, secure boot, device identity): designed (architecture §9), not built; the trial deploys a tarball with release directories and rollback, the same idea at small scale.
- **Supervisor MCU and controller firmware:** design only (architecture §7); the C client's protocol layer is written to carry over.
- **Driver work** (CPTS patch, sensor driver): the board's kernel differs from the SDK's (6.12.17 vs 6.12.57), so kernel modules need a matching build or a new image; left for camera bring-up.

## 5. Found on the way (the useful surprises)

| Finding | Effect |
|---|---|
| A 5 V USB-A supply resets the board when Linux loads drivers | 65 W USB-C PD required (setup §1) |
| Wave5: rate control off by default, strict input caps, level only through caps, lost/repeated PTS at stream start | mandatory settings; the index is checked again after the encoder (architecture §3, §4.4) |
| eMMC sync latency up to ~1 s under a 10 Mbps stream (throughput 96 MB/s says nothing about it) | soak 1 lost 7.9 % of frames; storage now behind a writer thread and queue; soak 2 lost 0 |
| A binding window of ± half a period lets a late frame take the next edge's index | window 3 ms, refused at a quarter period; timing alone can't be fully safe, the sensor frame counter is the real fix (DL-27) |
| The CPTS makes periods in whole 2 ns ticks: an odd period (30 fps) drifts 30 ppb off the plan | periods are whole ticks (DL-09) |
| The trigger output doesn't follow PTP's frequency correction (G4) | driver patch planned |
| Emulator rendering cost more than a 60 fps period on the A53; first-touch page faults delayed the first frames | pre-rendered frames, buffers touched at allocation |

## 6. Positions on the Architecture Context

Details in architecture §11.4 and §11.5. In short:
- **Changed:** recordings are fetched by the PC directly from the nodes, not through the controller.
- **Refined:** the index is computed from the hardware edge time, not counted at the trigger source.
- **Kept, verified:** no supervisor MCU on the standard tier: the J722S CPTS generates and timestamps the trigger itself (with G4 to fix in its driver).
- **Proposed:** sync budget of at most 50 µs camera-to-camera; open, with the other TBDs in §11.4.

## 7. What next, in order

From architecture §11.2; each step unblocks the next.
1. **Real camera chain:** CPTS driver patch (G4) and a 30-min PTP-locked rerun; trigger to the connector pin + logic analyzer; sensor driver, overlay, ISP; binding calibrated on the camera (latency spread, window, sensor frame counter).
2. **Robustness on one node:** recovery tests (unplug, power pull), PTP lock check, complete health and events, days-long runs.
3. **Several nodes:** second board, PTP-aware switch, controller fan-out from `fwt-rec`'s protocol layer; skew per index across nodes; absolute accuracy; conformance suite.
4. **More sensors per node:** two cameras (Wave5 ~100 fps at 1920x1200: 2 x 60 fps does not fit), camera + IMU on the same PTP time base.
5. **Product platform:** Yocto, A/B updates, secure boot, identity, interface security.
6. **Lite tier:** RV1106 + supervisor MCU as edge source; time base chosen by the sync budget.

Alongside: a **security track** (threat model first; authentication and network hardening with several nodes; secure boot, signed updates, identity and encrypted recordings with the product platform), a richer camera emulator and a CI / hardware-in-the-loop rig.
