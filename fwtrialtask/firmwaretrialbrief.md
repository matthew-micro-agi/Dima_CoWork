# Firmware: 2-Day Trial Project

## Documents in this pack

1. **This brief:** what we're asking you to do.
2. **Architecture Context:** the system, what we've decided and why, and what's still open.

If something is missing, treat it as part of the problem: decide, write down your assumption, and keep going. Ask questions whenever you have them, but don't wait on the answers.

---

## 1. Why you're here

We're building a modular capture system of hardware-synchronized cameras. The firmware is the product. The hardware is meant to be commodity and second-sourceable. The hard, defensible work is in capture, timing, the imaging chain and the control plane.

We're looking for a very senior engineer with high ownership: someone who takes a hard problem end to end, from architecture to code running on silicon, and whose judgment we can rely on without close supervision.

This trial is a short version of that work. Take an early-stage system with real constraints and open decisions, form an opinion, commit to an architecture, and prove one piece of it works on real silicon.

You have two days. Matthew, an engineer on our team, is on site and is your first contact for questions about the bench, the hardware or anything else. Otherwise you work on your own, and we review your output at the end. How you plan and scope your own work against a fixed deadline is a large part of what we assess.

---

## 2. The system

Read the Architecture Context. In short:

- About 10 synchronized cameras today, several packs of about 10 later. The cameras sit on a head-mounted device and look at the wearer's hands.
- Each camera is a self-contained recording tile: a camera module (B0) with an onsemi AR0235 sensor and an IMU, connected to a node board that encodes H.265 in hardware and records to its own eMMC as a rotating ring of segments. Recent segments can be locked against reclaim when something interesting happens.
- A hardware trigger starts every frame, so all cameras are frame-aligned. Every frame carries a trigger counter, so a dropped frame can't silently misalign a camera.
- A controller runs the fleet over Ethernet. It never touches video. Retrieval is a request to each node, not a mount.
- Two node tiers, a TI-based standard node and a cheaper Rockchip-based lite node, must be interchangeable behind one control interface. That interface isn't written yet.

Nothing is built yet. No custom hardware, no firmware.

---

## 3. What you have

- **This pack.**
- **The bench at your site.** A TI J722S EVM (J722SXH01EVM), cameras, and ESP32 devkits. There is no Nordic devkit. ESP32 is a supported Zephyr target and is the closest substitute. Finding out what is there and what works is your first step. Matthew can help you find things.

The J722S EVM has a quad Cortex-A53, 2× Cortex-R5F, 8 GB LPDDR4, 32 GB eMMC, four MIPI CSI-2 camera connectors on two CSI2RX instances, the Wave5 hardware encoder (H.264/H.265 via V4L2), gigabit Ethernet with TSN, PCIe and USB 3.0. That makes it closest to a gateway board (B2), so it can stand in for both the recording node and the gateway.

---

## 4. Scope

**In scope:** all of the firmware and software. Architecture, the control interface, timing, storage, platform, test, and hands-on work on the bench.

**Out of scope:** hardware design and board modification. If you need a cable, adapter or part we don't have, tell us what and why. Again, Matthew will help you to find it.

**If something we should have prepared blocks you, that's on us.** That includes hardware that isn't on the bench or doesn't work. Note it in your log, say what it blocked and what you skipped because of it, and move to something else. Don't sit stuck for half a day, and don't drop a deliverable without telling us why.

---

## 5. What we're asking for

Three deliverables, weighted equally. A great document with nothing running scores no better than working code with no plan behind it.

You won't finish everything in two days. **We care more about how you cut scope than about how much you finish.** Say what's done, what's stubbed, what you dropped, and why.

### 5.1 Firmware architecture and plan

A document a firmware colleague could pick up and build from. Decisions with reasons, not length.

- **Architecture:** what runs on the media SoC, on the supervisor MCU where there is one, and on the controller. Why, and what crosses each boundary.
- **Control interface:** draft the contract that makes two different node implementations interchangeable. Trigger semantics, how a frame binds to a trigger index, the command set, health and telemetry, failure and degraded-mode behaviour, versioning and compatibility. Write it so it can be tested, not as prose.
- **Time and data model:** how you show that camera 7's frame 41200 and camera 2's frame 41200 are the same instant. How a dropped frame is detected instead of silently absorbed. How a recording is indexed so "the last 90 seconds" is a cheap query. How event lock behaves against a ring that is constantly reclaiming space.
- **Platform:** Linux distribution and build system for the node, kernel and driver strategy including whether you upstream, Zephyr workspace layout, update and rollback, secure boot and device identity, and how you avoid getting locked into a vendor SDK.
- **Test:** what you automate, what needs hardware in the loop, and what a CI rig looks like for a system whose defining property is timing across many devices.
- **Plan:** critical path, what works at 4 weeks, 3 months and 6 months, and the five biggest risks with mitigations.
- **Open questions:** take a position on the ones in section 12 of the Architecture Context that touch firmware.


### 5.2 One working vertical slice

Pick the thinnest end-to-end path that proves the hard parts are real, and make it run on the J722S EVM. Our suggested target, which you can replace:

A camera streams into the board, is encoded to H.265 in hardware, and is written to eMMC as a rotating ring of segments with an index. A trigger source drives frame timing. Every recorded frame is bound to a trigger index that survives into the stored stream. A control interface can list the ring, fetch a time range, and lock segments against reclaim.

You won't finish all of that. Say what you built, what you stubbed and what you dropped, and keep the dropped parts easy to add to what you built.

Keep it narrow. A path that runs reliably, with numbers behind it, beats a broad one that ran once.

### 5.3 Handover

- **A git repository** with a readable history, and a README a competent colleague can follow to reproduce your result from a blank board.
- **Numbers, not claims.** Show the number behind everything you assert: sustained bitrate, CPU load, frames captured versus dropped over a run of at least 30 minutes, write throughput, trigger interval jitter, recovery when something is unplugged. Pick what matters and measure it.
- **A decision log:** choices, alternatives, reasons and dead ends.
- **A summary we can read in 30 minutes:** what works, what doesn't, what you skipped and why, and what you'd do next, in what order.

---

## 6. Disagreeing with us

The Architecture Context is our current position, written without a senior firmware engineer in the room. Disagree with any of it: the tiers, the controller, the supervisor MCU and where it belongs, the trigger and counter scheme, per-tile storage, the silicon.

"I'd change X, and here is what it costs and what it buys" is a strong answer. So is "I looked hard at X and would keep it, because..." Silent agreement is weak. So is rewriting everything into a stack you know without engaging with why ours looks the way it does.

Section 11 of that document marks which decisions are firm and which are preferences. Push on preferences freely. To move a firm one, make a real case.

---

## 7. Practicalities

- Two working days, self-directed. Matthew is on site for questions.
- Use whatever tools, references and AI assistance you normally use. We care about the result and your reasoning, not about working from memory.
- Nothing on the bench is precious.
- Hand over the repository and documents at the end. We review asynchronously, so it has to stand on its own.
