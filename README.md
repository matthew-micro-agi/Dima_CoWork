# fwtrial: hardware-triggered H.265 recording node (TI J722S)

Firmware for one camera node of a synchronised multi-camera recorder, built on a TI J722S EVM in a two-day trial. A trigger edge, generated and timestamped in hardware on PTP time, starts every frame; each frame is encoded in H.265 on the SoC's Wave5 encoder, carries its trigger index and edge time inside the stream, and is recorded to the eMMC as a ring of segment files with a manifest. A control interface (HTTP + CBOR, contract in CDDL) arms sessions, locks ranges and serves recordings.

**Start here:**
- **`docs/summary.md`**: summary: what works, what doesn't, what was skipped and why, what next.
- **`docs/numbers.md`**: every measured number, how it was measured, how to repeat it.
- **`docs/architecture.md`**: the design; §1 opens with the backbone (one PTP time base -> one plan -> edges in hardware -> frame bound to its edge -> index in every frame).
- **`docs/decision-log.md`**: choices, alternatives, reasons, dead ends (DL-01 ...).
- **`docs/setup.md`**: every bench step with commands, verify and undo lines.

## Record and watch (node deployed, `setup.md` §8.6)

```sh
build-host/src/tools/fwt-rec/fwt-rec 10.42.0.132 --duration 10 --out run1.h265
```
Records 10 s at 30 fps (`--fps 60` for 60), fetches the session into `run1.h265`, writes `run1.h265.json` (node, versions, session, frames, missing) and checks the file: ends with `check: passed ... pixels ok` and `OK`.
```sh
ffplay run1.h265                                         # watch
ffprobe -v error -count_frames -select_streams v:0 \
  -show_entries stream=nb_read_frames,width,height -of csv=p=0 run1.h265
ffmpeg -framerate 30 -i run1.h265 -c copy run1.mp4      # MP4 for any player, no re-encode
build-host/src/tools/sei-dump/sei-dump run1.h265         # per frame: session, index, edge time
```
A raw `.h265` stream has no timing of its own: give the recorded frame rate to `ffmpeg` (`-framerate 60` for 60 fps), else players assume 25 fps. Each frame's trigger index and edge time sit in its SEI (`docs/decision-log.md` DL-16); `sei-dump` reads them.

## Status in one table

| Part | State |
|---|---|
| Trigger: CPTS edges on PTP time, captured in hardware (`trigd`) | working; edges measured (device-tree overlay, `setup.md` §7); interval = period within 2 ns |
| Time: PTP client on the node (`fwtrial-ptp4l`) | working; 41-52 ns RMS to the bench grandmaster (the laptop) |
| Capture + encode + per-frame index (`nodecap`) | working with a test source and a camera emulator (`cam-emu`); Wave5 1920x1200 at 30 and 60 fps |
| Recorder: segments, manifest, ring, locks, crash recovery | working; 30-min soaks at 60 fps with a 500 MB ring |
| Control interface (`nodeagent`, ICD v1 in `icd/fwt-icd-v1.cddl`) | working; laptop client `fwt-rec` (C) |
| Deploy: node bundle, host bundle for operators, rollback, uninstall | working |
| Real camera | **not working**: no I2C answer from the sensor module on the bench (hardware gap); the camera emulator stands in |
| Several nodes in sync | **described, not shown** (one board) |

## Reproduce from a blank board

You need: the J722S EVM with TI's Edge AI SDK 11.0 SD card image (as shipped), a 65 W USB-C supply, an Ethernet cable to a Linux laptop (Ubuntu 24.04, built-in Ethernet port with hardware timestamping), and the TI Processor SDK 11.02.02.05 for cross-building. Details and pitfalls: `docs/setup.md` §1.

Every step below is a section of `docs/setup.md` with its commands, a **Verify** line and an **Undo** line. `tools/fwt-doctor` checks each machine read-only and points to the section that fixes a problem.

| # | Step | `setup.md` | Check |
|---|---|---|---|
| 1 | Bench hardware, serial console | §1, §2.1 | console reaches `j722s-evm login:` |
| 2 | Laptop: TI SDK, packages, Python venv | §2.2-2.4 | `tools/fwt-doctor host-dev` |
| 3 | Network link laptop <-> board | §3 | `tools/fwt-doctor host-dev root@10.42.0.132` |
| 4 | Board baseline (image as shipped) | §4 | `tools/fwt-doctor dev root@10.42.0.132` |
| 5 | PTP: laptop as grandmaster | §5 | board journal: `SLAVE`, offsets in ns |
| 6 | eMMC `/rec` partition (wipes the eMMC) | §6 | `findmnt /rec` |
| 7 | Device-tree overlay: CPTS timestamp loop | §7 | `fwt-doctor dev`: `CPTS timestamp loop` OK |
| 8 | Build, unit tests, end-to-end checks | §8.1, §8.2 | each check ends `ALL PASSED` |
| 9 | Deploy the node service | §8.6 | `DEPLOYED <version>` |
| 10 | Record from the laptop | §8.4 | `check: passed ... pixels ok`, `OK` |

The commands of steps 8-10, from this repo's root:
```sh
cmake -S . -B build-host && \
cmake --build build-host -j && \
ctest --test-dir build-host --output-on-failure
```
```sh
tests/trigd-check.sh dev root@10.42.0.132
tests/nodecap-check.sh dev root@10.42.0.132
tests/recorder-check.sh dev root@10.42.0.132
tests/nodeagent-check.sh dev root@10.42.0.132
tests/camemu-check.sh dev root@10.42.0.132
```
```sh
deploy/deploy.sh root@10.42.0.132 && \
tools/fwt-doctor dev root@10.42.0.132
```
```sh
mkdir -p ~/rec && \
build-host/src/tools/fwt-rec/fwt-rec 10.42.0.132 \
  --duration 30 --fps 60 --out ~/rec/run1.h265
```
Each check script also has a `host` mode (laptop only, no board). Board runs pause the installed service and start it again.

**Operators** (deploy and record, no build tools): `deploy/make-host-bundle.sh` makes `dist/fwtrial-host-<version>-x86_64.tar.gz`; its `README` is the whole procedure (`setup.md` §8.7).

## Repository

| Path | What |
|---|---|
| `src/trigd/` | trigger daemon: arms the CPTS periodic output, reads hardware edge timestamps, publishes `{session, index, t}` |
| `src/nodecap/` | capture + encode: frame sources (`test`, `emu`), binding frames to edges by time, Wave5 via GStreamer, SEI per frame, writer thread |
| `src/recorder/` | segments, manifest, ring, locks, recovery (library, inside `nodecap`) |
| `src/nodeagent/` | control interface (HTTP + CBOR), starts `trigd` + `nodecap` per session; `gen/` = code generated from the ICD |
| `src/common/` | shared: trigger index, message formats, SEI, drawn index, clock conversion, emulator protocol |
| `src/tools/` | `fwt-rec` (record from a laptop; start of the controller code), `sei-dump` (stream checker), `cam-emu` (camera emulator, tests only), `rec-tool`, `trig-listen`, `fwt-info` |
| `icd/fwt-icd-v1.cddl` | the control-interface contract (CDDL) |
| `tests/` | unit tests (`ctest`) and end-to-end checks (`*-check.sh host|dev`) |
| `tools/` | `fwt-doctor` (read-only pre-checks), `icd-json` (CBOR <-> JSON, laptop) |
| `deploy/` | node bundle, host bundle, `deploy.sh`, `install.sh` (install, rollback, status, uninstall), systemd units, default configs |
| `dts/` | device-tree overlay (CPTS timestamp loop) |
| `cmake/` | SDK toolchain file, version stamping |
| `prereq/` | pre-coding check tools (`testptp` build) |
| `logs/` | raw working notes of the trial, written along the way with AI assistance and committed as is: bench problems (`0000`), open tasks (`0002`), component notes (`0003`), measurement log with raw results (`0004`), dependencies (`0005`). Not curated and partly outdated: `docs/` is the reference; `logs/` shows how results were reached |

Versions: `0.1.0-g<commit>`, `-dirty` for uncommitted builds; `fwt-doctor dev` shows what the board runs against the checkout.
