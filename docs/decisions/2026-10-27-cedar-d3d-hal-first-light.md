# Direct3D through the HAL on the HD 5450: V9XD32 passes at 16 and 32 bpp

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boots 8 to 16, the ati build with the Evergreen Direct3D engine
(`d3d_eg.c`, `d3d_eg_map.c`, `engines\eg_cp.c`). Tool: `V9XD32.EXE`, which
reads back every pixel it draws, at 640x480x32 and with `/bpp16`.
Captures:
[`../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/).
Item 5 of [the 3D plan](../plans/ati-evergreen-cedar-3d.md). The streams
are the ones V9XEG3D ran first
([record](2026-10-27-cedar-d3d-streams.md)).

## The first result

Boot 16, both depths, no engine timeouts (the final result, boot 19, is
below):

| Test | 16 bpp | 32 bpp |
|---|---|---|
| T1 flat | 0 mismatches | 0 mismatches, `0013579B` |
| T2 Gouraud corners | 7 of 7 | 7 of 7 |
| T3 depth | pass | pass |
| T4 offscreen target | 0 mismatches | 0 mismatches |
| T5 precision | pass (Z16 cannot tell them apart, as expected) | pass |
| T6 ARGB1555 texture | **fail**: the format is not offered, the draw refused | **fail**, the same |
| T7 SRCALPHA blend | pass, average `847D00` | pass |
| T8 sloped depth | pass | pass |
| T9, T10 | pass | pass |
| T11 culling, six submit forms | all `3F` | all `3F` |
| T12 two stages | skipped: one stage offered | skipped |
| T13 volume | PASS | PASS |
| T14 a child that exits mid-scene | PASS | PASS |
| T15 memory outside the driver | PASS | PASS |

Overall `Result=FAIL` comes from T6 alone.

## Three faults the card found, in order

**1. The first draw hung the 3D pipeline (boots 8 to 12).** V9XD32 drew
nothing, one idle timeout was counted, and the fault trace named
`CP_RB_RPTR`. The CP was started correctly: `SCRATCH_REG0` held the ring
test's `DEADBEEF`. Tagging each quarantine site showed the wait for ring
space (`EG3P`), with the CP's read pointer 64 dwords past ours. A log of
the last 16 submissions, and the ring dumped at the quarantine
(`V9XEGCP.TXT`, still written on any quarantine), showed the CP stopped
176 dwords into the second batch, waiting for a context while the first
batch's draw never finished.

The first batch was overwritten once the ring wrapped. With each submit
made to wait for the one before (a diagnostic build, not kept), the first
batch survived intact (boot 12). Decoded, its shaders pointed at
`0x00015000` rather than `0x0F015000`. The draw had computed the shader
and heap addresses before the CP's lazy start located the window, so the
first batch named offsets from zero, and the GPU ran desktop pixels as
its shaders. The addresses are now located on first use by a plain read
of `MC_VM_FB_LOCATION`, before any mapping (`v9x_eg_cp_addresses`).
`accepts()` can use that read too, without starting the CP. Run b13 looked
like a second failure but was not: the reset's reply timed out, the guest
had not restarted, and the old HAL's quarantined CP refused every draw.
The boot counter showed it.

**2. Nothing drew at 32 bpp (boots 13 to 15).** Every batch was refused
by the builder. `DrawsTargetLast=0`: after a flip, V9XD32's back buffer
is the surface at heap offset 0, and the heap starts at GPU address 0 on
this card. The builder took address 0 to mean "no target", and likewise
"no depth" and "no texture". `eg_3d.h`'s state now carries `has_depth`
and `has_texture`, and accepts address 0. The host test checks a target
and a depth buffer at 0.

**3. 32 bpp pixels were `FF13579B`, not `0013579B` (boot 15).** The pixel
shader exports alpha, and the CB wrote it into the XRGB8888 target's X
byte. `CB_TARGET_MASK` is now `7` (B, G, R) unless the state asks for
alpha writes, as Gen3's alpha writes are off. The host test was written
first and failed to build until the field existed.

## Then: 1555 and 4444 textures, and T4 placed (boots 18 and 19)

ARGB1555 and ARGB4444 are now offered and sampled as `FMT_1_5_5_5` and
`FMT_4_4_4_4`, with no swizzle. T6, an 8x8 ARGB1555 texture of four
colours, passed at both depths on boot 18, every quadrant exact: red
`F800` / `FF0000`, green, blue and white. That settles the field order:
the sampler's x is the low field, blue, as the shaders take it. 4444 has
no test yet.

The same boot broke T4, the 64x64 offscreen render target, at both
depths: two `TARGET` refusals. Now that T6's texture was placed on 256
bytes, DirectDraw's 8-byte placement put T4's target off the CB's 256-byte
base alignment. Until then the alignment had held only by luck. The engine
now places an offscreen render target itself, pitch to 8 pixels and
256-aligned. It leaves a flip chain's back buffers to the heap, which puts
them after the primary on whole screens.

Boot 19, two runs at each depth: **V9XD32 `Result=PASS`** at 640x480x32
and x16, T1 to T11 and T13 to T15 all passing, T12 skipped (one stage), no
engine timeouts.

## Also in this step

- The mini-VDD maps 4 MiB at BAR0 + 240 MiB. The DMA ring keeps the first
  64 KiB. The CP's 16 KiB ring, its write-back slots and the five shaders
  follow, and the vertex arena is the top 2 MiB (`eg_3d.h`,
  `V9X_EG_WINDOW_*`). `cedar_hw16.c` stamps `CAP_D3D` only for a window
  that size.
- The CP is started by the first batch, once per HAL, with V9XEG3D's
  sequence. The DMA engine and the CP wait for each other:
  - a batch waits for queued DMA work;
  - `eng_eg.c` drains the CP before a fill or copy and in `wait_idle`.
- A Z16 buffer is 1D tiled and placed by `create_surface` (pitch rounded
  to 8 pixels, rows to 8). A depth fill is taken only for the whole
  surface, written over its padded block: a constant is right whatever
  the tiling. A partial depth fill is declined, and the core then fails
  it rather than fill linearly.
- AMD's CP microcode is linked into V9XHAL.DLL from this build.
  `NOTICES.TXT` reproduces LICENSE.radeon, as its terms require (decision
  1 of the plan).
- V9XHAL grew enough that the ATI floppy's disk 1 was 3,919 bytes over.
  `V9XTRACE.EXE` now rides disk 3 with `V9XUPD.EXE`, following Michael's
  2026-10-24 ruling that a third disk is fine for files nothing needs to
  boot or draw.

## Not measured

- ARGB4444 textures: offered, sampled the way 1555 measured, not drawn by
  any test.
- Mip chains (the sampler reads level 0), specular, fog and colour keys:
  not offered or refused.
- Texel phase against Direct3D's convention (V9XDDP TexPhase); the PA is
  told whole-coordinate centres.
- Any game, 3DMark, or more than one Direct3D program at a time beyond
  T14.
- Throughput. Every batch re-sends the whole default state and flushes the
  caches.
