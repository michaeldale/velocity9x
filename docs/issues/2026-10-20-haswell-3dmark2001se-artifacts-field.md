# Haswell, a tester's HD 4600: artifacts in 3DMark 2001 SE (report V9X-D9VZS6)

Date: 2026-10-20. Status: open. Not reproduced; nothing changed.

Source: report V9X-D9VZS6 from V9XTRACE's upload, filed 2026-10-20
05:20, no contact address. Evidence:
[`../probe/field-hsw-gigabyte-3dmark-2026-10-19/`](../probe/field-hsw-gigabyte-3dmark-2026-10-19/)
(the four attached files, unmodified, hashes checked against the
server's).

## The machine

| | |
|---|---|
| IGD | `8086:0412` rev 06 (HD 4600), SUBSYS `1458:D000` (Gigabyte), bus 0 dev 2 fn 0 |
| Windows | 98 SE (4.10.2222 A), DirectX 9.0c runtime (4.09.00.0904), 2 GiB |
| Driver | 0.20.0 `16503bf2`, all binaries agree; updated from 0.19.0 by V9XUPD (`State=committed`) |
| Desktop | 1024x768x32 through VBE mode `0118`, pitch 4096 |
| CPU | not recorded; the TSC ran at 3.19 GHz over the window, as on our i5-4570 |

This is not the i5-4570 in `docs/STATUS.md` (a Dell); it is the first
HD 4600 outside this project.

## What the tester reported

In the report's own words: "artifacts in 3dmark 2001se, poor performance
and skipped bump mapping in 3dmark 2000, Occasional lag on UI elements
and mouse dragging on the desktop in 32bpp, cant change display refresh
rate". No screenshot, no test name, no settings.

Only the first is new. The other four are recorded as already explained,
below, and are not what this record is open for.

## What the snapshot shows

One snapshot, `V9XSNA1.INI`, taken at 20:07:08, 929 s after boot. The
timers cover 849 s from the first D3D call. 3DMark 2001 SE ran from
43.4 s to 448.0 s of uptime (21 D3D contexts), 3DMark 2000 from 542.7 s
to 921.3 s (5 contexts). Both processes ended normally (`WatchNEnded=2`);
no fault, no message box.

Clean:

- 0 engine FIFO or idle timeouts, 0 resets, 0 ring-space timeouts; the
  engine was still valid at the dump (`EngineFlags=1`), so no render-wait
  timeout quarantined it.
- 26 contexts and 27,118 textures created, all destroyed.
- 0 textures refused for format, shape, bounds or caps; 0 DP2 records
  unparsed or malformed; 0 FVFs refused; 3,964 of 3,964 Z buffers
  accepted.
- 12,814 flips handled, 12,792 taken at the done poll, 0 declined.

The two counters that bear on artifacts:

1. **4,863 batches refused by the engine** (`BatchesEngineRefused`), of
   2,769,434 batches sunk (0.18%). The core returns `DD_OK` for these
   and draws nothing (`d3d_core.c`, each `batches_engine_refused`
   site), so each is a hole in a frame. It is not a mapping refusal:
   `v9x_d3d_hsw_draw_batch` counts those in `M64Policy00`-`19`, and all
   twenty are 0, as is `M64PolicyLast`. That leaves the other `return 0`
   paths in `v9x_d3d_hsw_draw_batch` (not ready, a blitter or render
   wait, the state write, the workload build, the submit) and whichever
   core path refuses before the engine is called. The Haswell path has
   no counter that names the site, so the snapshot cannot say which.
   Which 3DMark test the refusals fall in is also unknown: the counters
   cover both runs.
2. **Mip chains sampled at level 0.** 46,938 of 2,521,865 chain checks
   found a level not where `v9x_intel_hsw_miptree` puts it
   (`MipGapOffset`, last expected `0xE6D52000`, actual `0xE6793000`);
   each such texture is sampled at level 0 with no mip filter
   (`d3d_hsw.c`, `v9x_d3d_hsw_mip_levels`). 10,851 tree placements were
   declined, the last for reason 4, `V9X_D3D_PLACE_ALLOC`: the heap had
   no room for the block. Level-0 sampling gives shimmer and aliasing at
   a distance; whether the tester calls that "artifacts" is not known.

Not looked at further here: `D3dTargetPitch=2112` for a 1024-wide target
(2048 + 64). `v9x_intel_hsw_surface_2d` accepts it, so it does not
explain a refusal.

## The other four symptoms, as the snapshot and source explain them

- **Poor performance.** 51% of the window was inside the engine draw:
  2.77M batches at about 158 µs, 54 triangles each. About 77 µs a batch
  was the two synchronous render-engine waits (`TimeRenderDrain`, 5.56M
  calls; `AsyncEnabled=0`) and about 59 µs the vertex copy
  (`TimeRingWrite`). `V9XBOOT.INI` shows the explicit UC MTRR over
  `E0000000` and the refused write-combining plan (`r=11`) recorded in
  `docs/decisions/2026-10-18-haswell-batch-cost-uncached-kernel-build.md`.
  The same cost on a second machine; nothing new.
- **3DMark 2000's bump-mapping test skipped.** By design:
  `d3d_core.c` reports `wMaxSimultaneousTextures = 1` and the HAL offers
  no DOT3 or EMBM. `Dp2Stage1States=0` agrees.
- **Desktop lag at 32 bpp.** `GdiAcceleration=none`: GDI is drawn by the
  CPU, into the same uncached aperture. Listed in `docs/STATUS.md`.
- **No refresh-rate choice.** `ModeSwitching=vbe-lfb`: the video BIOS
  sets the mode and its timing. Listed in `docs/STATUS.md` ("Modes are
  the video BIOS's").

## Next

Without a contact address the tester cannot be asked for the test name
or a screenshot. Two things would let the next report answer it:

- a per-site refusal counter on the Haswell batch path, so
  `BatchesEngineRefused` names its cause;
- a 3DMark 2001 SE run on the i5-4570 at 1024x768x32, with a snapshot
  before and after each test, to see which test the refusals and the
  declined trees fall in and what is on screen there.

## 2026-10-20: the heap is 16 MiB on every HD 4600

The declined mip trees' last reason, `V9X_D3D_PLACE_ALLOC`, is
`DDHAL32_VidMemAlloc` returning nothing: DirectDraw's heap was full
(`d3d_place.c`, `v9x_d3d_place_block`). The heap is sized by
`v9x_vbe_vram_bytes`, which `v9x_vram_usable_bytes` (`enable16.c`)
clamps to the DPMI mapping, and the Intel family maps 16 MiB of GMADR
(`intel_hw16.c`, `0x00ffu, 0xffffu`). `V9XBOOT.INI` shows it:
`VddReserve ... vram=16777216` beside `VbeController ... mem=1023`
(64 MiB from 4F00h). Our i5-4570 reads `vram=16777216` too, against
32 MiB stolen.

The benchmark ran at 1024x768x16 with three buffers: the flip offsets
are 0, `0x0018C000` and `0x00318000`, each 2112 x 768 bytes
(`D3dTargetPitch=2112`), and the Z buffer follows at `0x004A4000`,
2048 x 768. That is about 6.2 MiB, which leaves about 10 MiB for every
texture 3DMark 2001 SE loads. So a texture-heavy test running out of heap is
the expected outcome on this driver, not a fault. Nothing measured says
whether the heap running out caused what the tester saw.

Not changed. The Haswell engine reserve sits at aperture `1800000h`
(24 MiB, `docs/plans/intel-haswell-direct3d.md`), above the 16 MiB
heap. The 945GSE and 915GM share this table entry, and their stolen
memory is 8 MiB on the machines measured. Mapping more of the aperture
on the HD 4600 alone would be per-chip hw16 data, and the reserve
would have to move above whatever is mapped. That is a design
decision, not taken here.
