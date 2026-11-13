# Numbers

Every measured number behind a claim, with how it was measured and how to repeat it. Bench: TI J722S EVM (J722SXH01EVM, SoC J722S SR1.0 HS-FS), TI Edge AI 11.0 SD image (kernel 6.12.17), node software `0.1.0-g13c96fa`; laptop Ubuntu 24.04 as PTP grandmaster and client. Measured 2026-11-12/13. Board address `root@10.42.0.132`.

Not measured are listed at the end, with why.

## 1. Recording over 30 minutes (the headline)

| | Soak 1 (before the storage fix) | **Soak 2 (as built)** |
|---|---|---|
| Rate, size, bitrate | 60 fps, 1920x1200 NV12, 10 Mbps target, Wave5 | same |
| Trigger edges | 108,001 (computed times) | 108,001 (hardware-measured, see §5) |
| Frames recorded | 99,477 | **108,001** |
| Missing | 8,524 (7.9 %) | **0** |
| Frames bound to a wrong index (`sei-dump` pixel check on the fetched end of the session) | 0 of 24,897 | **0 of 20,881** |
| Ring | 500 MB, wrapping from ~6.5 min | 500 MB, 451 segments closed, 429 reclaimed |
| PTP | locked to the laptop | locked to the laptop |

- **Path:** the installed service (`nodeagent` -> `trigd` + `nodecap` + recorder), frames from the camera emulator `cam-emu` (no real camera, §8), recording to the eMMC `/rec`; laptop `fwt-rec --duration 1800 --fps 60`, a status poll and a timed "last 90 s" fetch every 5 min, a drop watchdog every 20 s.
- **Ground truth:** `cam-emu` draws each frame's true trigger index into the picture; `sei-dump` decodes the fetched file and compares it with the index `nodecap` found by timing alone.
- **Soak 1's losses, cause measured:** eMMC syncs (keyframe `fdatasync`, segment close) took up to 1 s; they ran under `nodecap`'s shared lock, capture waited (max 712 ms), the camera's 4 buffers (67 ms at 60 fps) ran out. Fix: a writer thread with a 16 MB write queue (architecture §5.0, `src/nodecap/nc_wq.c`). Same 60 s test before / after: 3,273 / **3,600** of 3,600 frames.
- **Repeat:** temporary service settings (500 MB ring, `--src emu`, 10 Mbps) + `cam-emu` on the board, then `fwt-rec 10.42.0.132 --duration 1800 --fps 60 --out soak.h265`; short version: `tests/camemu-check.sh dev root@10.42.0.132` (600 frames at 60 fps, recorder on the eMMC, every frame stored).

## 2. CPU, memory, temperature (soak 2, 30 min, 60 fps)

| Process | CPU (of one A53 core) | RSS max | Threads |
|---|---|---|---|
| `nodecap` (+ recorder, writer thread) | 33.3 % | 39 MB | 5 |
| `trigd` | 1.0 % | 1.2 MB | 1 |
| `nodeagent` | 0.4 % | 3.6 MB | 2 |
| **Load: our processes** | **34.7 % of one core = 8.7 % of the 4 cores** | | |
| `cam-emu` (frame source, not load) | 14.0 % | 43 MB | 1 |

- SoC temperature 61.9-65.8 °C (max of the thermal zones); memory available ~6.1 of 6.5 GB throughout.
- **How:** a shell sampler on the board every 10 s reading `/proc/<pid>/stat`, `/status`, `/proc/stat`, thermal zones, `/proc/diskstats`; deltas over the run.

## 3. Storage (eMMC, 32 GB Micron, HS200)

| What | Number | How |
|---|---|---|
| Sequential write, 1 MiB direct + fsync | 96.5 MB/s (need 1.2 MB/s at 10 Mbps: ~80x headroom) | `dd` on a freshly discarded `/rec`; best case |
| 4 KiB writes, queue depth 1 | 36.6 MB/s (~9,000/s) | `dd` |
| Sequential read, 1 MiB | 173 MB/s | `dd` |
| 4 KiB reads | 43.7 MB/s (~10,700/s) | `dd` |
| Recording write rate | 1.20 MB/s (10 Mbps stream) | soak sampler, `/proc/diskstats` |
| **Sync latency under a 10 Mbps recording** | keyframe `fdatasync` avg 66-156 ms, **max 0.7-1.2 s**; segment close max 0.65-0.87 s; segment open incl. ring reclaim max 126 ms | `nodecap`'s `timing:` line (timed in the recorder) |
| Write queue (16 MB) during those stalls | peak 1.6 MB, overflow 0; capture waited max 2.4 ms | `timing:` line |
| Wear | EXT_CSD life time A/B 0x01 (0-10 % used), pre-EOL normal | `/sys/block/mmcblk0/device/` |

- **Lesson:** throughput is not the limit; sync latency is (a single sync can take a second). Design rule: never sync on the capture path.

## 4. Encoder (Wave5, H.265)

| What | Number | How |
|---|---|---|
| Throughput, 1920x1200 | ~100 fps (600 frames in ~5.8 s) | `gst-launch-1.0` from a pre-rendered noise file (P-1) |
| Bitrate on noise (worst case) | ~22 Mbps at a 10 Mbps target (60 fps), ~9 at 5 (30 fps) | P-1 |
| Bitrate in the soak (emulator content: moving pattern + noise) | ~9.4 Mbps at a 10 Mbps target | segment sizes / fetch sizes |
| Keyframes, headers | 1 per second, VPS/SPS/PPS before each; a segment cut at a keyframe decodes alone | P-1, `sei-dump` |
| Level | 5 (signalled 150), set through caps | `ffprobe` |
| Pipeline start | 33-35 ms | `nodecap` log |

- Mandatory settings found on the way (rate control off by default, strict caps, level) are in architecture §3.

## 5. Trigger timing (CPTS, hardware-generated and hardware-captured edges)

| Condition | Result | How |
|---|---|---|
| 60 fps, PTP not correcting the frequency, 600 edges | every edge captured; every interval = period (0 ns at the 2 ns resolution); constant +14 ns from T0 + k·P (routing delay) | `trigd --edges auto` + `trig-listen` interval statistics |
| 30 fps, odd period 33,333,333 ns | **found:** every interval 1 ns short (the CPTS makes whole 2 ns ticks) -> 30 ppb drift off the plan | same |
| 30 fps after the fix (period 33,333,334 ns) | every interval = period (0 ns) | `tests/trigd-check.sh dev` (now a pass/fail step) |
| **60 fps, 30 min, PTP locked** (soak 2) | **found (gap G4):** every captured interval ~950 ns long = 57 ppm, the crystal error PTP corrects; edges drift off PTP time by 57 µs/s; past a quarter period `trigd` flags edges `TS_MISSING` (53,596 of 108,001) | `trigd` stop line, `trig-listen` on the board |
| Fallback without a timestamp input | warning after 3 edges, computed times, 0 edges lost | `trigd-check dev` |

- **G4 cause** (driver source `am65-cpts.c`): PTP's frequency correction is applied to the time counter and only to the kernel's own PPS output; `trigd`'s periodic output keeps none. Recording, binding and indices are unaffected; edge alignment across nodes is not met until the driver is patched (architecture §11.2 step 1).
- **Repeat:** `tests/trigd-check.sh dev root@10.42.0.132`; with PTP locked: laptop grandmaster (setup §5), then `trigd` + `trig-listen` for 30 min.

## 6. Time sync (PTP, board as client of the laptop)

| Run | Offset RMS | Max \|offset\| | Frequency | Path delay |
|---|---|---|---|---|
| P-3, 60 s | 30 ns | 85 ns | -56.8 ppm | +7.49 µs |
| Soak 1, 30 min (28 x 64 s summaries) | median 52 ns (47-1559) | 7.9 µs (one disturbed minute) | -51.8 to -58.4 ppm | 7487-7500 ns |
| Soak 2, 30 min | median 51.5 ns (42-1613) | 7.8 µs | -52.5 to -58.4 ppm | 7487-7511 ns |

- Laptop as grandmaster needs `phc2sys` (its Intel I219 clock runs ~1600 ppm off). Default 1588 profile, not yet gPTP. Absolute accuracy not measured (G2).
- **How:** `fwtrial-ptp4l` summaries (`summary_interval 6`) from the board's journal.

## 7. Frame-to-edge binding by time (camera emulator)

| Run | Result |
|---|---|
| Clean, 600 edges at 60 fps, board, recorder on the eMMC | 600 bound, 0 spurious, latency 24.5-25.5 ms (set 25 ± 0.5), every frame stored |
| Faults 3 % drop / extra / late, 2 % sequence jump | counts match the emulator's plan exactly (gaps = dropped + late, spurious = extra + late); **0 frames with a wrong index** |
| Window ± half a period (first design) / ± 5 ms | 1 frame with a wrong index (a late frame took the next edge whose own frame was dropped) -> window 3 ms, refused at a quarter period (DL-27) |

- **Repeat:** `tests/camemu-check.sh host|dev`.

## 8. Ring, locks, fetch

| What | Number |
|---|---|
| Ring wrap, small ring (4 MB, 2 Mbps) | 900/900 frames while wrapping; closed session reclaimed first; locked segment survived; oversized lock refused (`recorder-check`) |
| Ring wrap, 30 min, 500 MB | `/rec` steady at ~505-513 MB; 429 segments reclaimed without a lost frame (soak 2) |
| Lock vs reclaim, 30 min | a 69-75 MB lock survived every wrap and was fetched at the end (soak 1); a second lock refused by the 20 % cap (soak 2) |
| **"Last 90 s" fetch while recording and reclaiming** (60 fps, ~113 MB) | **first byte 1.5-8 ms, complete in 0.87-1.02 s** (12 fetches over both soaks) |

## 9. Deploy and boot

| What | Number |
|---|---|
| Node bundle / host bundle | ~424 KB / ~580 KB |
| Services back after a reboot | ~25 s from the reboot command to the control interface answering |
| Deploy, rollback, uninstall, reinstall | each verified on the board (`deploy.sh`, `install.sh --rollback`, `--uninstall`) |

## 10. Not measured, and why

| What | Why not |
|---|---|
| A real camera (latency, timestamp spread, ISP, bitrate on real content) | the bench sensor module answers on no camera connector (G3) |
| Trigger edge at the connector pin | edge not muxed to the pin yet; needs a logic analyzer |
| Several nodes in sync, sensors exposing together | one board in the trial (scope); also needs G4 fixed |
| Absolute PTP accuracy | needs PPS + scope (G2) |
| Recovery: power pull, Ethernet unplug (M10) | not run in the time; process kill + restart and crash recovery of the recorder are covered by `recorder-check` / `trigd-check` (pass/fail) |
| Runs longer than 30 min | time; planned (days-long) |
