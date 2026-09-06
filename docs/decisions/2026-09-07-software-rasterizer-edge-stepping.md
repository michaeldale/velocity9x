# Exact edge stepping cuts the cost of small software triangles

Date: 2026-09-07. Implemented and measured in existing 86Box guests only.

The software rasterizer now computes each edge's division quotient and
remainder once, then advances all eight attributes by addition and carry.
Small-triangle scene time falls from 42.78 to 19.36 ms in RAM on both guests.
This is a useful scalar improvement, not a game compatibility or frame-rate
claim. No physical hardware was accessed.

## Method and evidence

Both guests use the existing YM430TX, Pentium MMX 200 MHz, dynarec-enabled,
128 MB configuration. These are emulated CPU settings, not a physical CPU
timing calibration. The other guest was not running a benchmark concurrently.

* `Win98SE-Trio64`, loopback port 9871: 800x600x16, 4 MB Trio64,
  `Direct3D=2`. Baseline/candidate/baseline ran during boot 332. The new HAL
  was then installed and verified during boot 333.
* `Win86SE`, loopback port 9869: ViRGE/DX, 1024x768x32, boot 572.
  RAM benchmark only; 32-bpp scratch surfaces deliberately fail the tool's
  16-bpp VRAM eligibility check. Its installed hardware HAL was not changed.

`V9XSOFT.EXE` links the production rasterizer directly. DirectDraw allocates
three scratch video-memory surfaces; it does not change the display mode or
draw onto the primary. This measures rasterization without D3D dispatch,
clear/presentation time or an application's other work. Each scene uses a
320x240 RGB565 arithmetic target and either 128 small triangles or one large
triangle. Point/bilinear scenes modulate a 64x64 RGB565 texture; Depth adds
LESSEQUAL and Z writes; Alpha also adds SRCALPHA/INVSRCALPHA. The latter
scenes therefore include the earlier work, rather than isolating one feature.

Three samples per workload run for at least 750 guest milliseconds each,
after a warm-up frame. Reported times are the median of milliseconds divided
by completed frames. Buffers are cleared outside the timed interval. A fresh
single frame after timing supplies colour and Z hashes, independent of how
many timed frames completed. The RAM/VRAM comparison places the target,
texture and Z buffer together in that memory class; it does not isolate which
of texture and Z should move to RAM first.

The baseline links the rasterizer captured from `f21d703`; the candidate
links this change. Both use identical Watcom options, matching the HAL's
non-optimizing code generation except for the executable rather than DLL
target. Build IDs are `soft-base` and `soft-edge`. The HAL is `soft-edge-01`.

Raw reports and CSV summaries are retained in
[`docs/probe/software-d3d-2026-09-07`](../probe/software-d3d-2026-09-07/).

## Results

Trio64, median milliseconds per synthetic scene:

| Scene | RAM before | RAM after | Speedup | VRAM before | VRAM after | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 128 small Gouraud triangles | 42.778 | 19.359 | 2.210x | 43.611 | 20.270 | 2.151x |
| Large Gouraud triangle | 23.438 | 20.132 | 1.164x | 25.862 | 22.879 | 1.130x |
| Point texture | 87.222 | 84.444 | 1.033x | 118.571 | 115.714 | 1.025x |
| Bilinear texture | 167.000 | 164.000 | 1.018x | 283.333 | 283.667 | 0.999x |
| Bilinear + depth | 175.000 | 171.200 | 1.022x | 323.333 | 320.333 | 1.009x |
| Bilinear + depth + alpha | 209.750 | 206.250 | 1.017x | 387.000 | 385.000 | 1.005x |

The repeated baseline is within 0.7% of the first baseline for all scene
medians. All 24 colour/Z hashes match across the three Trio64 runs. The
ViRGE RAM control gives 2.210x for Small and 1.163x for Gouraud, with all 12
hashes matching; see its CSV for the remaining workloads. Sub-percent VRAM
changes in the heavy scenes are noise-sized, not established improvements.

The memory sweeps read or write 153,600 bytes per pass using volatile 16-bit
accesses. Baseline medians are RAM read 4.658 ms, VRAM read 62.692 ms, RAM
write 5.067 ms, VRAM write 9.679 ms. These include loop overhead. Emulated
aperture reads are much more expensive than RAM reads in this configuration;
the result does not establish PCI/VLB bandwidth or cache behavior on real
cards. It motivates a separate RAM-texture/Z experiment, not a residency
policy change in this patch.

## Correctness and installed-driver check

The new edge carries `floor(delta / span)` and a nonnegative remainder.
Its initial value is the old exact interpolation at the first pixel centre.
For every subsequent sixteen-subpixel step, it adds a whole increment and
carries the accumulated remainder. No reciprocal approximation or wider
integer is required. The short edge is initialized afresh at the middle
vertex's first applicable sample. Edges are never stepped beyond their last
sample. Horizontal and clipped-empty triangles still draw nothing.

Span gradients remain per span. Replacing them with ideal per-triangle
gradients would change the existing quantized edge endpoints and can change
pixels, so that part of the earlier scalar proposal is deferred.

Validation:

* Existing Watcom host tests pass, including shared-edge alpha, descending
  interpolation, full-height depth and extreme wrapped coordinates.
* A new frozen corpus covers 2,048 independently cleared triangles, two target
  formats, point/bilinear textures, all eight depth comparisons, depth write
  masks, alpha, subpixel edges, flat edges and clipping. Expected colour/Z
  hashes, including padding/guards, were generated with the original source.
  They pass under both Watcom and MSVC. MSVC retains its explicit unrelated
  skip for the Watcom-only x87 converter.
* `run-checks.ps1 -BuildId soft-edge-01` passes: structure, survey safety gate,
  host tests, all family builds, binary/INF audits and transfer packages.
* The same `SOFTPROB.EXE` ran against the installed original and candidate HAL
  on Trio64. Both reports say `COMPLETE`; `compare-probe.ps1` reports zero
  unexpected differences after excluding volatile fields. Texture-alpha and
  mip/trilinear failures remain unchanged; the private-Z probe was not run.
  `COMPLETE` does not mean all advertised or unimplemented features passed.

Only `C:\WINDOWS\SYSTEM\V9XHAL.DLL` was replaced, via checked-empty
`WININIT.INI` and reboot. The downloaded installed DLL matches the built
43,520-byte candidate. Existing SYSTEM.INI settings were preserved. The old
42,496-byte HAL is saved locally as
`build/software-d3d/trio64-original-hal.dll` for rollback. Candidate SHA-256
and executable identities are recorded alongside the raw evidence.

## Remaining work

The tool makes future scalar changes measurable without a driver install.
Texture-heavy pixels still dominate: pursue exact modulation arithmetic and
bounded loop specialization next, with these hashes unchanged. Measure mixed
RAM texture/Z residency and a named game's end-to-end behavior separately.
Physical timing, very short triangle setup costs, texture alpha, perspective
correction, mip selection and fog remain open. No capability bits or defaults
changed, and this work is not a release.
