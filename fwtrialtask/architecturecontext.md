# Architecture Context

Companion to the trial brief: what we've decided, why, and what's still open. Read sections 11 and 12 last, but do read them. The decision log shows what's firm, and the open questions show what you can decide yourself.

---

## 1. The product

A rig of synchronized cameras that records continuously to local storage and offloads afterwards. The cameras sit on a head-mounted device and look at the wearer's hands. All cameras expose on the same trigger, so their streams are frame-aligned for downstream reconstruction.

Each camera is a self-contained recording tile: camera module, media SoC, encoder and its own storage. A central controller runs the tiles over a network. Today that is about 10 cameras in one pack. Later it will be several packs of about 10, and eventually no fixed limit.

---

## 2. Capture requirements

| | Minimum | Standard tier |
|---|---|---|
| Resolution | 1920x1200 native, 1080p window | Same |
| Frame rate | 30 fps at full resolution | 60 fps |
| Shutter | Global, color (Bayer) | Same |
| Bit depth | 8-bit output, 10-bit at the sensor is fine | Same |
| Codec | H.265 Main, 8-bit 4:2:0 | Same |
| Session length | 8 h | 24 h |
| Storage per camera | 32 to 64 GB eMMC | 128 to 256 GB eMMC |

- An external hardware trigger is required. Free-run is nice to have.
- Exposure, gain and white balance must be directly settable, and every auto mode must be possible to switch off. Matching cameras matters more than per-camera cleverness.
- H.264 is out. AV1 is out for now, because no low-power hardware AV1 encoders exist in this class. We revisit when they do.

Bitrates we size from (H.265, good quality):

- 1080p30: about 5 Mbps (0.6 MB/s), or 18 GB per camera per 8 h
- 1080p60: about 8 to 10 Mbps (1.2 MB/s), or about 100 GB per camera per 24 h
- Low-bitrate mode: 2 to 4 Mbps, or 20 to 40 GB per camera per day, which offloads over 100 Mbit Ethernet in under an hour

**Sensor:** onsemi AR0235: 1/2.8-inch, 1920x1200, 2.8 µm global shutter pixels, color. MIPI CSI-2 on 1, 2 or 4 lanes, up to 850 Mbps per lane. It runs at up to 120 fps free-running, but 90 fps at full resolution when hardware-triggered, which still covers our 60 fps.

**Optics** are settled and outside your scope: integrated lens, HFOV at least 110°, VFOV at least 80°, landscape, IR-cut, fixed focus.

---

## 3. Boards

We name the boards B0 to B4. Two tiers exist side by side and must be interchangeable.

| Board | Standard tier | Lite tier | Role |
|---|---|---|---|
| **B0** camera module | AR0235 color global shutter sensor, integrated lens, IMU, calibration EEPROM. FSIN, MIPI CSI-2, I2C and SPI on a board connector, cabled to the node | Same B0 | Sensor, optics, IMU, per-unit calibration |
| **B1** recording node | TI AM62A3 (chip-down), LPDDR4, eMMC, PMIC, dual RGMII/SGMII to the switch | **B1-lite:** Rockchip RV1106 (stacked DRAM, built-in 10/100 Ethernet), eMMC, nRF54L15 supervisor MCU | Capture, ISP, H.265 encode, loop recording, network |
| **B2** gateway camera | B1 capture chain plus cellular (M.2 over USB2), 10G NIC over PCIe, WiFi | **B2-lite:** Rockchip RV1126, gigabit Ethernet, USB2, WiFi | A camera that is also the link to the outside world |
| **B3** orchestration board | nRF54L15, LAN9668-class TSN switch, 48 V PoE distribution | None | Fleet control, switching, time master |
| **B4** | nRF54L15-based | None | Out of scope for this trial |

B0 comes from camera module suppliers, built to our spec. The IMU always sits on B0, fixed to the sensor and lens. No node board has one.

**Nothing is built.** Our only capture hardware is evaluation hardware, mainly a TI J722S EVM. It is closest to a B2: it has the capture chain plus gateway I/O (PCIe, USB 3.0, gigabit TSN Ethernet), so you can exercise both the node role and the gateway role on one board. It is a stand-in, not a production choice (section 12).

### Why only the lite tier has an MCU

**Standard tier: the TI SoC handles the trigger.** The AM62A3 has hardware 1588 timestamping on its Ethernet MACs. It syncs its clock over the network (gPTP) and drives or reads the FSIN edge itself. B1 has no supervisor MCU.

**Lite tier: the RV1106 can't.** Its 10/100 MAC has no documented 1588 timestamping, and PTP on its vendor kernel is unproven. Software PTP over Fast Ethernet is off by tens to hundreds of microseconds and varies with load. A trigger pulse generated from Linux would sync worse than a wire. So B1-lite adds an nRF54L15, the same MCU as on B3, which handles:

- FSIN: generates or receives the edge in hardware (GPIOTE, TIMER, DPPI), with no Linux in the path, and timestamps it
- Trigger counting: passes counter and timestamp to the RV1106 to embed in the stream
- The link to the controller: RS-485 on the existing cable, or nRF-to-nRF 2.4 GHz time transfer
- Power sequencing, watchdog and power-cycle recovery of the SoC, update rollback, secure identity
- Optionally, reading B0's IMU over SPI. IMU samples and FSIN edges then share one clock, which makes camera-to-IMU alignment free instead of one of the hardest parts of a reconstruction pipeline.

Two hard rules: the supervisor never touches video (no video bandwidth, no USB host role; video goes SoC to Ethernet), and it never grows into a second application platform. It is a supervisor, a timekeeper and a protocol endpoint, nothing more.

**The controller can't tell B1 from B1-lite.** Both present the same control interface (section 6). Different silicon, different ways of hitting the trigger, one contract.

---

## 4. Timing and sync

The whole product depends on this section.

**Every frame is hardware-triggered.** Sensors run in slave mode, and an edge on FSIN starts each exposure. Because every frame is re-pinned, crystal drift between sensors can't build up. A shared sensor clock (INCK from one oscillator through a buffer) would remove the remaining ppm offset and give nanosecond alignment, but it is optional.

On the AR0235, the FSIN edge goes into a GPI or GPIO pin set up as trigger input, in one of four slave modes. A GPIO can also output a flash signal that marks the exposure, so sync can be checked directly.

**Within a pack, we distribute an edge.** A hardware timer drives a fanout buffer or an RS-422/485 driver (about $0.50 per node) to every sensor's FSIN. Buffer skew is tens of picoseconds. The real error is cable length mismatch, at about 5 ns per meter, so we match lengths. That gives well under a microsecond. The trigger always comes from a hardware timer, never from a GPIO toggled by software.

**Between packs, we distribute time.** Packs sync over gPTP on wired Ethernet, and each pack generates its own trigger from synced time. That needs about 5 gPTP nodes for 50 cameras, not 50. Adding packs means adding switch ports. Packs are 2 to 3 m apart.

**gPTP needs hardware timestamps at the MAC or PHY.** That rules out WiFi (no deterministic PHY timestamp, and Zephyr doesn't run gPTP over it) and USB networking (CDC-NCM/ECM is emulated in software). A timestamping PHY can still sit behind a compact connector, or use Single Pair Ethernet. 10BASE-T1L (ADIN1100/ADIN1110) keeps full gPTP at 10 Mbps, enough for sync and control. 100BASE-T1 is the step up if we need more.

**Every frame is bound to a trigger index.** Each frame carries a trigger counter in its stream metadata, and the trigger source logs the counter against UTC. Without this, one dropped frame silently misaligns a camera for the rest of the session and nothing downstream can tell. It is the most important line in the system, and the part we most want you to get right.

A side effect: with one trigger source, timestamps stay global even on nodes that can't run PTP.

---

## 5. Storage and retrieval

- **Each node stores its own video** on its own eMMC. eMMC has a single host, so the SoC that writes it owns it, and nothing else mounts it. The controller never touches video. It sends triggers and commands, a few kbps, which keeps it small.
- **Why per-tile storage:** a central recorder caps the camera count at its inputs and bandwidth and needs a heavy host or an FPGA. Per-tile storage lets the number of cameras grow. Files move to a central store after each session, not during it.
- **eMMC, not SD or raw NAND.** SD has weak wear levelling, a connector that fails, and fakes on the market. Raw SPI NAND pushes flash management (UBI/UBIFS, bad blocks, wear levelling) into our firmware, and continuous recording is exactly the workload that punishes that. eMMC handles wear levelling and ECC, industrial grades report health and endurance, and the SoC boots from it, so there's no separate boot flash. It costs a couple of dollars more per node.
- **Loop recording by segment rotation:** the node encodes into fixed-size segment files of 1 to 10 s and reclaims the oldest when the ring is full. Segment files rather than one circular file work with the filesystem and the eMMC's wear levelling instead of hammering one region. An index makes "the last T seconds" a cheap lookup.
- **Event lock:** a command, an external trigger or a detected event locks recent segments so the ring won't reclaim them. Think dashcam incident protection. On a synced rig it sets us apart.
- **Retrieval is a request, not a mount:** the controller asks each node to list segments or send the last 90 seconds, and the node reads its own storage and serves the bytes. The controller is a client of N small file servers and forwards onward to a PC. Its load is bounded by its upload link, not by the number of cameras.
- **Offload:** Ethernet is the backbone. The gateway can also act as USB mass storage, push over WiFi, or use cellular. Full-quality live video from 10 cameras over WiFi doesn't work, because WiFi is shared and half-duplex. Instead each node records full quality locally and streams a low-bitrate preview (1080p15 at 3 to 5 Mbps, about 40 Mbps for ten).
- **Dock rule:** upload has to be faster than recording, or the dock's cache just fills up. Ten standard-tier cameras produce about 100 Mbps, so a gigabit dock has headroom and a 100 Mbit uplink only works in bursts.

---

## 6. Control interface

One interface contract between the controller and any node. Both tiers implement it. It isn't written yet. It has to cover at least:

- Trigger: who generates and receives it, arm and disarm, rate changes, behaviour on loss
- How frames bind to trigger indices, and where the counter sits in the stream
- Commands: enumerate, configure, start, stop, list segments, fetch a range, lock and unlock, query time
- Health: storage health and ring depth, dropped frames, temperature, link state
- Failure behaviour: losing the controller, losing the trigger, full storage
- Versioning and compatibility rules, and a conformance test

Interchangeable tiers only work if this contract is testable. A prose description won't survive two independent implementations on different silicon.

---

## 7. What we own and what we buy

This shapes the firmware work more than any technical choice.

- **We own the imaging chain:** sensor characterization, the sensor driver, ISP tuning, the calibration pipeline, and the trigger and metadata path. This is our core value, and we build it ourselves.
- **We own the firmware.** Vendors don't write it.
- **Hardware is commodity and second-sourceable.** Board design is contracted as work-for-hire, with native files at every milestone and the right to manufacture anywhere. B0 comes from camera module suppliers.
- **Factory calibration is delegated.** The factory runs it on their own station. We own the spec: EEPROM map, camera model and coordinate conventions, targets, acceptance limits, golden samples, a reference dataset their station must reproduce, and an offline checker we run on the records. Raw captures come back to us, so we can recalibrate and update units in the field without the factory.

---

## 8. Networking and power

- Ethernet carries control, sync and bulk data. More cameras means more switch ports.
- B1 has dual RGMII/SGMII, for daisy-chain or redundant links. B1-lite has one Fast Ethernet port, so it runs in a star. A three-port switch on the module, about $1, would allow chaining.
- B3's LAN9668-class switch is a full 1588/TSN device and can output a PTP-synced pulse.
- 48 V PoE carries power on the same cable.
- We want to avoid RJ45 because of its size. The plan is a timestamping PHY behind a compact connector, or Single Pair Ethernet.

---

## 9. Platform notes

- **Standard tier:** AM62A3, quad Cortex-A53, LPDDR4, Wave5 H.264/H.265 encoder, TI VPAC ISP with the VISS/DCC tuning flow. Good English documentation, a longevity program and FAE support. The AR0235 is not in TI's supported-sensor list, so a V4L2 driver and an IQ tuning pass are ours to do.
- **Lite tier:** RV1106, single Cortex-A7 at 1.2 GHz, 128 to 256 MB stacked DDR3L, 5 MP ISP, H.265 encode up to 3072x1728 at 30 fps, two 2-lane MIPI CSI ports that combine into one 4-lane port. Open SDK, which matters as a way out if the vendor stops helping. Compared to TI we lose headroom, not function: no room for large pre-trigger raw buffers or heavy on-camera computer vision, and a vendor kernel plus community support instead of a supported toolchain. The encoder margin is thin: 1920x1200 at 60 fps is about 138 Mpix/s against a rated 159 Mpix/s. Confirm it early. The RV1126 is the fallback if the RV1106 is too tight.
- **B2-lite:** RV1126 rather than RK3568, because the RK3568 encoder tops out at 1920x1088 and would force the gateway's own camera to crop 112 lines, which breaks field-of-view and calibration uniformity. The RV1126 passes 1920x1200 untouched and shares the nodes' ISP and software family, so sensor driver, tuning files and firmware are shared. It has no USB3 and no PCIe.
- **One sensor everywhere,** currently the AR0235. Mixed sensors break color and shading uniformity, which hurts reconstruction. Two ISP tunings (TI and Rockchip) behind one calibration pipeline are acceptable.
- **Zephyr** (nRF Connect SDK) runs on the supervisor MCU and on B3. We want one workflow across the fleet.

---

## 10. What we're not doing

- **No FPGA.** Considered for sync and multi-camera aggregation, and rejected on purpose.
- **No Zephyr-only video path.** No single Zephyr part does dual MIPI, hardware H.265 and storage. Video runs on Linux SoCs, and Zephyr runs the deterministic control side.
- **No central real-time recording.** It would force a heavy aggregator.
- **No PCIe/NVMe storage.** Too much power, for throughput we don't need at these bitrates.
- **No entity-listed silicon.** HiSilicon was offered and rejected, because we plan to sell globally.
- **No full-quality live video over WiFi.** Preview stream only.

---

## 11. Decision log

| Decision | Why | How firm |
|---|---|---|
| Per-tile storage | Only way to scale the camera count with a light controller | **Firm** |
| Hardware trigger on every frame, sensors in slave mode | No drift build-up. Sync becomes a wiring problem, not a software one | **Firm** |
| Trigger counter in every frame's metadata | Otherwise one dropped frame silently misaligns a camera | **Firm** |
| Controller never in the video path | Keeps the controller on a small MCU and bounds its load | **Firm** |
| One sensor across the fleet, currently the AR0235 | Mixed sensors break uniformity | **Firm** |
| IMU on B0, never on a node board | Fixed to the sensor and lens | **Firm** |
| eMMC | Managed flash, no connector | **Firm** |
| H.265 | No low-power AV1 encoders yet | Firm for now |
| Segment-based loop recording | Flash-friendly and easy to index | Strong preference |
| Two tiers, one interface | A price range without forking the software | Strong preference. How is open |
| gPTP between packs, wired edge within | Best sync for the effort | Strong preference |
| Supervisor MCU on the lite tier only | TI has 1588 built in, Rockchip doesn't | **Open** (section 12) |
| AM62A3 for the standard node | Picked before we had the J722S EVM | **Open** (section 12) |

---

## 12. Open questions

Nobody will answer these during the trial. Where they affect your work, pick a position, write it down, and move on.

1. **Which silicon for nodes and gateway?** The J722S on our EVM (also sold as AM67A) is a bigger sibling of the AM62A3, with the same Wave5 encoder and the same TI V4L2/GStreamer stack, so work done on it transfers. It adds four camera inputs on two CSI2RX instances, PCIe and USB3. It suits B2 and is oversized for B1. Should the gateway use a bigger part than the nodes, or the same part with more populated? And with four camera inputs on one SoC, should a node serve one camera or several? Several would change the tile model, the sync design and the cost curve together.
2. **Should the standard tier get the supervisor MCU too?** We currently say no: TI does it natively, and an MCU adds cost and a second codebase. The case for yes is that both tiers would then run identical control code rather than two implementations of one spec. We'd like your opinion on this.
3. **What is the sync budget in microseconds?** We've said "milliseconds is enough, tighter is better" but never set a number. It decides how much of the timing design we really need.
4. **Ring depth and bitrate targets** for the first product. Section 2 gives the range, not a commitment.
5. **The controller link on the lite tier:** RS-485 on the existing cable, or nRF-to-nRF 2.4 GHz time transfer with scheduled triggering. The second keeps the cable simpler but is unproven for us.
6. **Updates, rollback and secure identity** are named but not designed.

---

## 13. Glossary

- **B0 to B4:** our board names (section 3).
- **FSIN:** frame sync input, our name for the trigger into the sensor. On the AR0235 it's a GPI or GPIO pin set up as trigger input.
- **INCK:** the sensor's input clock, called EXTCLK on the AR0235.
- **Tile or node:** one camera's module, SoC, encoder and storage.
- **Pack:** about 10 tiles in one place, sharing one wired trigger.
- **Supervisor:** the small MCU on a lite node.
- **Controller:** the board that runs the fleet (B3). Also called the orchestrator.
- **Ring and segments:** the loop recording buffer and its fixed-size files.
- **ICD:** interface control document, the written contract between controller and node.
