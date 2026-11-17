# The Oland's CPU waited for the GPU three times a frame: flips queued behind their fences

Date: 2026-11-17

Machine: W9QI0DAD (AMD FX-6300, Radeon Oland XT 1002:6610), 1024x768x32,
3DMark 2000 default run under V9XPROF (one sample a millisecond of the
main thread). Builds are main `c03121cf` and its working tree, all
`-dirty`. Evidence: `build/driver-results/oland-prof-b95-*`,
`oland-queued-b96`, `oland-arena-b97`, `oland-cpfill-b98`,
`oland-final-b99` (not in the tree: the profiles are 4 MiB each).

## What the profile showed (boot 95, main)

With the vsync the game asks for, score 3959:

| where | share of samples |
|---|---:|
| `eg_scanout`: the flip-pending poll (two CRTC reads a call) and its siblings | 22 % |
| KERNEL32 (DirectDraw's Flip retry loop runs through it; not resolved) | 18 % |
| `d3d_vi` vertex copy into the arena | 11 % |
| `eng_vi`: SDMA fence wait | 9 % |
| `vi_cp`: ring copy 5 %, ring space 4 %, CP fence wait 5 % | 14 % |

3DMark asks for synced flips, and most of the first 40 % is the
application spinning on Flip until the blank: correct behaviour, not a
cost the driver adds. With `[Velocity9x] VSync=2` (score 8157) the vsync
spin goes and what is left is the driver: `v9x_vi_cp_poll_fence` 14.3 %,
the ring-space wait 7.4 %, the ring copy 7.4 %, the vertex copy 15 %.

Where the CPU waited for the GPU, by reading the code against the hot
instructions:

1. **Every Flip drained the GPU.** `v9x_flip_body` runs `v9x_wait_idle`
   and `v9x_render_drain` unless `v9x_scanout_flip_queued()` says the
   scanout queues flips, and only the Rage 128 did. The CPU built
   nothing while the GPU finished the frame.
2. **Every 2 MiB of the vertex arena drained the GPU** before its start
   was written again.
3. **Every clear drained the GPU.** The frame's colour and Z clears go
   to SDMA, and `v9x_vi_after_cp` drains the CP first; the next batch
   then waits for SDMA (`v9x_vi_dma_settle`).

## What changed

- **Queued flips** (`eg_scanout.c`, GCN only: engine type VI, so Carrizo
  and Mullins too). Flip records the frame's end - the CP's last fence
  and SDMA's last sequence (`v9x_vi_frame_mark`) - and returns. The
  address is written by the first poll that finds both engines past it:
  Flip, GetFlipStatus, a draw or fill waiting on the buffer on screen,
  or every eighth submission (`v9x_present_note_submission`). The
  register write is the measured one, unchanged (GRPH_FLIP_CONTROL
  clear, taken at the blank). Until it is written the flip is pending,
  unsynced or not, so the CPU runs one frame ahead and no further. A GCN
  draw into the buffer still on screen waits for the flip
  (`v9x_d3d_vi_flip_guard`), as fills already did (`v9x_blt_flip_guard`).
  Evergreen's own CP keeps the drain.
- **The arena is reused a quarter at a time** (`vi_cp.c`): each quarter
  keeps the fence of the first batch submitted after it was left, and
  writing into it again waits for that fence only.
- **Two diagnostic register reads sampled one in 16:** the retrace
  duty-cycle sample on every batch (`d3d_core.c`, every engine; 1.2 %
  here) and the CP write-pointer readback on every put (0.8 %; no
  `V9XCPWPT.TXT` on this machine, so it has not fired).

New external symbols, all internal to the HAL: `v9x_vi_cp_last_fence`,
`v9x_vi_cp_fence_passed`, `v9x_vi_frame_mark`, `v9x_vi_frame_done`,
`v9x_eg_flip_queues`, `v9x_eg_set_display_start_now`,
`v9x_eg_flip_advance`, `v9x_scanout_flip_advance`.

## Measured

3DMark 2000 default run, VSync=2 unless stated, no frame grabs unless
stated. V9XD32 after each deploy: T1-T16 and T22 pass, T4's fills and
copies exact; the only failure is T19 FfSpecFog, 11h pixels, the known
issue (`docs/issues/2026-11-17-t19-ffspecfog-mismatch-oland-gt218.md`).

| boot | build | score | notes |
|---|---|---:|---|
| 95 | main | 3959 | game's vsync |
| 95 | main | 8157 | |
| 96 | queued flips | 8465 | `poll_fence` 14.3 -> 7.5 % |
| 97 | + arena quarters | 8454 | `poll_fence` 7.5 -> 6.8 % |
| 98 | + clears on the CP (dropped) | 8212 | 7437 with grabs |
| 99 | queued flips + arena + sampled reads | **8579** | +5.2 % over 8157 |
| 99 | the same | 3985 | game's vsync, against 3959; 3745 with grabs |

No run had an engine timeout, a forced-idle flip or a draw that waited
for a flip (`DrawsFlipWaited` 0: 3DMark triple-buffers). Frame grabs at
boots 98 and 99 (vsync off and on) show whole frames.

## The Intel machines

Only the shared part reaches them: the retrace sample every 16th batch
and the flip-advance hook, a no-op off GCN. Each ran the release-check
runner's probe set and a full 3DMark 2000 on 0.54.0 (`f93b15e3`, as
found) and on this build (`6a907a6f-dirty`, all eight files by WININIT,
hashes matched). Evidence `build/driver-results/perf-intel-*`.

| machine | 0.54.0 | this build | probes |
|---|---:|---:|---|
| Netbook, GMA 950 (VSync=2) | 1506 | 1511 | all pass, no verdict changed |
| i5-4570, HD 4600 | 4016 | 4011 | all pass, no verdict changed |

No draw into the presented buffer and no engine stall in either run;
the grabbed frames are whole. Windowed presents (DirectDraw / GL ms):
netbook 0.51 / 1.01 and 0.52 / 0.93, HD 4600 0.09 / 0.53 and 0.09 /
0.60. Two runs on the HD 4600 were lost to things outside the driver:
3DMark's own fault at 004E23D1 (the known over-read, no engine timeout),
and the NETGEAR WG111v3 utility, which opens a window after a reset and
takes focus, so 3DMark reported "Benchmark Aborted!". The runner's
2-second `wait-window` is now 5 s, v9xctl's least.

## Tried and dropped: clears on the CP

Boot 98 drew colour and Z fills of flip-chain and 3D-target surfaces on
the CP while it ran (a rectangle under the colour PS; 5:6:5 values widen
by bit replication, which round-trips through 8-bit, FP16 and the CB for
all 32 and 64 values, checked on the host). V9XD32 T4 and T14 were
exact and the frames clean, `poll_fence` left the profile, and the score
fell to 8212 from 8454. The CPU's wait moved to ring space
(`v9x_vi_cp_pending` 7.9 -> 18.4 %): with the drains gone the GPU sets
the pace, and a clear on the CP is no faster for it than one on SDMA.
Reverted.

## What it says

- The CPU-side serialisation at the flip and the arena was real and is
  gone; the score moved 5 % with vsync off.
- With the vsync the game asks for, nothing moved (3959 -> 3985, 14,700
  and 14,789 flips): 3DMark 2000 is held at the refresh on this card, so
  the CPU's time in Flip is the game waiting for the blank. A frame that
  takes longer than a refresh is where queued flips help a synced game.
- **The GPU is the limit now, not the CPU.** The biggest CPU item left
  is waiting for ring space (7-8 %, 18 % when the clears stopped
  draining), and the vertex copy into VRAM (15 %). Each batch is at most
  64 triangles (`V9X_D3D_INDEXED_BATCH`) and carries the whole state,
  a SURFACE_SYNC of the shader caches and an end-of-pipe fence: about
  3.9 M batches in a run. Fewer, larger batches, or state and syncs only
  when they change, is the next lever, and it is a GPU-side change that
  needs its own measurements.

## Not established

- One run per build; three builds in a row scored within 0.2 %, which
  is all that is known about run-to-run spread here.
- Carrizo and Mullins run the same scanout and CP code and have not run
  this build.
- KERNEL32's 18 % is not resolved to a caller.
- The vertex copy's cost per byte (about 380 MB/s average, against the
  2.9 GB/s write-combined writes measured on this box's GT218) is not
  explained.
