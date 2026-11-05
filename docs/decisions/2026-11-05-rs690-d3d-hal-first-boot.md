# RS690 Direct3D HAL by register writes: V9XD32 passes, after three fixes the probes had not exercised

Machine: M-GB8KRE (Sempron 210U, Radeon X1200 IGP 1002:791F, Win98 SE,
DirectX 6.1, so DDI 5 only). Branch `ati-rs690` at 8e9654c2 plus the HAL
work (`8e9654c2-dirty`), installed over the branch `ati` build that has
booted there since boot 56. The mini-VDD was left as installed: its
source did not change. 2026-11-05 ~10:00-11:00Z. Five restarts, all by
`reset`, all came back.

The evidence is in `docs/probe/rs690-d3d-hal-2026-11-05/`:

- V9XD32's result per boot, lettered a to e.
- The V9XTRACE snapshots.
- The register probe's quadrant-texture captures at both pitches.

## What was built

The Rage 128's split (`docs/plans/ati-rs690-x1200.md`, phase 5):

- **`src/display32/d3d/d3d_rs690_map.c`.** The neutral draw to
  `struct v9x_rs690_3d_state`. Host-tested: 126 checks, failing against
  a stub first.
- **`src/display32/d3d/d3d_rs690.c`.** It does four things:
  - Publishes the caps.
  - Resolves textures and places them and Z16.
  - Writes the state, then the TL vertices, eight dwords each, behind
    `VAP_VF_CNTL` in runs of up to 64 triangles. Every 16 writes wait
    for FIFO room first.
  - Ends each batch with the 3D and Z cache flushes and
    `WAIT_UNTIL 3D_IDLECLEAN`.
- **`eng_rs690.c`.** Its once-per-mode validation now writes the 3D init
  too. It writes it in two halves on an idle engine, and gives the D3D
  engine the MC base through `v9x_rs690_3d_begin`. That is the one new
  external symbol.
- **Settings.**
  - The core clips (`clip_in_core`) and moves Direct3D's pixel centres
    by half a pixel (`pixel_centre_half`).
  - The rs690 row now carries `EngineCaps` `D3D` and
    `Direct3D = 'hardware-rs690-pio'`.

## What each boot showed

| Boot | Build change | V9XD32 (640x480x32) | What it meant |
|---|---|---|---|
| a | first HAL | every pixel black, Z untouched; V9XTRACE M64Refused 272, all reason 14 | DX5's SPECULARENABLE is on by default, and the map refused specular. Direct3D skips a refused draw; it does not fall back |
| b | specular and flat built | colours drawn; T1, T2, T4, T14 failing; T1Pixel FF13579Bh for 0013579Bh | the chip writes the fragment's alpha into the XRGB8888 target's X byte |
| c | RB3D_COLOR_CHANNEL_MASK without alpha on an XRGB target | Failures 1: T6 only. Red read blue, green right, blue and white black | below |
| d | texture record (corner/centre texels, format, pitch) | the HAL resolved T6 right: ARGB1555, offset 2EE500h, pitch 32, memory FC00h / FFFFh as written | the chip, not the HAL |
| e | texture and Z pitches to 64 bytes | run 5 Failures 1 (T15, below); runs 6-9 Failures 0, at 32 and 16 bpp | |

Specular and flat shading went in as Mesa's encodings:

- **Specular:** colour 1 goes through interpolator 1 into the next free
  temp and is added as the ALU MAD's third operand.
- **Flat:** GA_COLOR_CONTROL 5555h, first vertex provoking.

V9XD32's T1 flat, T2 Gouraud, T4, T6 and T14 judge both.

## The 64-byte texture pitch

T6 is an 8×8 ARGB1555 texture; DirectDraw's placement gave it a 32-byte
pitch, which R300TextureSetup accepts. The register probe drew the same
texture (`TexQuadrants`) at a 64 KiB-aligned offset and at one ending in
500h. The result was the same wrong picture at both offsets and both
depths:

- Texel row 2 came from data row 4, so the red quadrant read blue and
  green read white.
- Texel rows 4-7 came from past the data, garbage that changed with the
  offset.

So the chip steps 64-byte rows whatever TX_FORMAT2 says, and alignment
is not the cause. Mesa's `r300_get_pixel_alignment` raises the RS690's
linear width alignment to 64 bytes; this chip is why. With the rows 64
bytes apart, the probe is exact at both offsets and depths. V9XD32's T6
then reads red, green, blue and white.

Every earlier probe texture was 32 texels wide, 64 bytes a row at 16 bpp
and 128 at 32. That is why the texture probe passed and missed it.

The builder now refuses a texture pitch that is not a multiple of 64
bytes, and placement pads to it. Z16 pitches go to 32-pixel multiples by
the same Mesa table. That is a margin, not a measurement: Z had passed at
1280 bytes.

## ARGB1555, ARGB4444 and specular on the chip

Added to the probe while finding T6, all at 16 and 32 bpp:

- `TexFormat1555` and `TexFormat4444` (the gradient read as each
  format) pass within 0.6 of a step.
- `TexSpecular`, a texture with specular on and a zero specular colour,
  is byte-exact. A specular of 40h added 40h to blue.

## T15: stray writes from a second process

Boot e's first V9XD32 run (run 5) found 796 heap words zeroed, first
seen after T14's child process. Runs 6-9 in that boot were clean.

The afternoon's hunt (boots 61-67) turned it into a reproducible,
per-boot fault: in some boots every run fails. The child's own pixels
reach physical RAM at 0600xxxxh:

- its colour fills of 0;
- its green triangles, written RGB-only over the original top byte.

Software Direct3D never does it. The register-only 3D probe and
DirectDraw alone never do it. Every MC address the HAL programmed lay
in the frame buffer, and the memory controller puts the frame buffer at
physical 78000000h (`K8_FB_LOCATION`, read through MC_INDEX).

Boots 68-71 found the trigger. A 3D draw that starts while a 2D fill
is still running misdirects colour-cache lines. Some land elsewhere in
the frame buffer, some in that system RAM. `WAIT_UNTIL` written over
MMIO does not hold the 3D engine back with the CP off. The register
probe reproduces this in the HAL's order and is clean with a CPU idle
poll between the fill and the draw.

The HAL now polls for idle before every 3D draw. In boot 71 that left
the window untouched through five children, and V9XD32 passed T15 in
all eight runs (`/bpp16` included). The details are in
`docs/issues/2026-11-05-rs690-t15-heap-words-zeroed-once.md`.

## Not checked

- No game or benchmark has run on the HAL.
- Fog is refused and counted (reason 15).
- No mip level past 0 is sampled.
- DECALALPHA and ADD are refused.
- The render interface (OpenGL, Glide) is all refused to the software
  engine.
- DDI 6 and later need a newer DirectX than this machine has.
- Throughput is unmeasured: 25 register writes a triangle, plus the
  state per batch.
- The monitor was not looked at.
