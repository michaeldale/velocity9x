# Gen3 adds the specular colour in its own programs, and BUMPENVMAPLUMINANCE is drawn where a luminance bump format was measured

Date: 2026-11-14. Follows
[`2026-11-14-dot3-and-bump-env-stage-ops.md`](2026-11-14-dot3-and-bump-env-stage-ops.md),
which left both open. Machines: the netbook (GMA 950, W9QI2BE2), boots
340 to 342, and the Dell i5-4570 (HD 4600), boots 260 to 262; the whole
intel-gma set by `run-release-checks.ps1` each time, built dirty from
`77495fa5`. Evidence:
[`../probe/gen3-specular-bump-luminance-2026-11-14/`](../probe/gen3-specular-bump-luminance-2026-11-14/).

## Gen3 specular

The first attempt took specular draws through the shader path and broke
3DMark 2001 SE's 8-light test (the earlier record). This one adds the
colour in Gen3's own fragment programs.

- **The stream is the fog stream.** It already declares T9 and carries
  the specular dword after the primary colour in every vertex (S4
  `SPEC_FOG`), and C0 is loaded. `V9X_I9XX_FOG_SPECULAR` in the `fog`
  argument asks for one instruction more, ahead of the fog blend:
  `add_sat R1.xyz, colour, T9` in the one-unit programs (`i9xx_fog.c`),
  `add_sat R2.xyz, R2, T9` in the two-unit one (`i9xx_multitex.c`).
  Three dwords. ADD and its saturate bit are the encodings the two-unit
  program's ADD has used since 2026-10.
- **Without FOGENABLE** the specular alpha is not a fog factor, so the
  engine sets it to FFh in the vertices it sends, and the blend that
  follows passes the colour through: `(1 - 1) * C0` and `1 * colour`.
- **Only where a vertex has a colour to add.** `SPECULARENABLE` starts
  on in the older interfaces; a batch whose speculars are all black
  keeps the short vertex and program.
- **The decoder**: the one-unit programs are licensed by length, three
  dwords more under the flag, on the 32-bit side only (`I9XXCODE` has no
  room and the 16-bit driver emits none). The two-unit grammar already
  took an ADD from T9 in a fog stream.
- **The caps**: `SPECULARFLATRGB` and `SPECULARGOURAUDRGB` in Gen3's
  ShadeCaps, for every DDI.

Host tests (`test_runtime_specular`) were written with the code, not
seen to fail first.

### Measured, the netbook

| Boot | What | Result |
|---|---|---|
| 341 | V9XD32 DDI 8 | `FfSpec` pass, worst channel error 9 of 9 (R5G6B5), ShadeCaps 00084208 |
| 342 | probe set | passes, no verdict changed; V9XD32 DDI 8 PASS |
| 342 | 3DMark 2001 SE, Bump Mapping tests | Environment Bump Mapping 10.1 fps, **DOT3 Bump Mapping 5.8 fps** |
| 342 | High Polygon Count alone | 0.7 / 0.6 MTriangles/s (1 light, 8 lights) |
| 342 | all 17 tests, 640x480 16-bit colour | **988**; High Polygon Count 0.7 / 0.6; Environment Bump Mapping 10.2 fps; DOT3 5.8 fps |

After the High Polygon run: nothing refused, no shader triangle dropped,
no malformed record, no fault (`V9XSNA7-netbook-boot342-highpoly.INI`).
Recorded, not compared.

`FfSpec` judges a textured MODULATE draw with FOGENABLE off against the
reference interpreter. The fog-on, untextured and two-unit forms were
judged later the same day (below). The 8-light test draws through it
and nobody has compared its frame with another card's.

## BUMPENVMAPLUMINANCE

- **The generator** writes `texbeml` where it wrote `texbem`, for a
  stage whose colour op is `BUMPENVMAPLUMINANCE`: the sampled stage's
  colour times the bump texel's third channel scaled and offset by the
  bump stage's `BUMPENVLSCALE` and `BUMPENVLOFFSET`. Both backends and
  the reference interpreter already had `texbeml`. Host test first,
  seen to fail.
- **Two formats carry that channel**: `D3DFMT_X8L8V8U8` (62) and
  `D3DFMT_L6V5U5` (61). Haswell: `X8B8_UNORM_G8R8_SNORM` (0E6h) and
  `R5G5_SNORM_B6_UNORM` (119h), codes from Vol 2a. Gen3:
  `MT_32BIT_XLVU8888` and `MT_16BIT_BUMP_655LDVDU`, Mesa's `i915_reg.h`.
- **How the runtime names them**: the DirectX 8 runtime hands a texture
  of either to the driver as `DDPF_BUMPDUDV | DDPF_BUMPLUMINANCE`
  (000C0000h) with a bit count and du, dv and luminance masks, not as a
  format code. Read from the refused draws of boots 260 and 340, where
  only the code was matched and every luminance draw was dropped
  (`V9XPS-hsw-boot260-refused-tail.txt`).
- **What is published is what measured clean**
  (`limits->bump_luminance`), and `D3DTEXOPCAPS_BUMPENVMAPLUMINANCE`
  with it:

| Machine, boot | X8L8V8U8 (`FfBemL8`) | L6V5U5 (`FfBemL6`) |
|---|---|---|
| HD 4600, 261 | mismatch, all 984 judged pixels, worst error 122 | pass, worst error 9 of 9 |
| GMA 950, 341 | pass, worst error 0 | 1 pixel of 984 wrong, by 66 in a channel |

  So Haswell publishes L6V5U5 and Gen3 X8L8V8U8. On the final build
  (boots 262 and 342) each machine's probe passes its row and skips the
  other.

Dead on the way:

- **du and dv over 16, not 15, in L6V5U5.** A probe judging by 16 had
  100 and 104 mismatches on the two machines (probe alone replaced,
  boots 261 and 341). Fifteen it is; the netbook's one pixel is
  something else, not found.
- **That Haswell samples `X8B8_UNORM_G8R8_SNORM` as its name reads.**
  The Haswell volumes have no table of what the sampler reads and name
  this format nowhere in Vol 7; what comes back was not decoded.

Ivy Bridge shares Haswell's code, has run neither row and publishes
neither format. Nothing was run that uses the op outside the probe: no
game or benchmark here is known to set it.

## Before the release: the other specular forms, older DirectX, and a two-stage draw half a pixel off

The same evening, after `3ea75094`. Evidence in `before-release/` of the
probe folder.

**Four more T19 rows**, each drawn with no shader bound and judged
against a program written in the probe: `FfSpecFog` (FfSpec under vertex
fog), `FfSpecU` (no texture, stage 0 selecting the vertex colour),
`FfSpec2` (two textures, each modulating what came before, then
specular) and `FfTwo` (the same two textures, no specular).

| Machine, boot, build | FfSpecFog | FfSpecU | FfSpec2 | FfTwo |
|---|---|---|---|---|
| HD 4600, 262, `3ea75094` | pass (9) | pass (9) | pass (9) | pass (9) |
| GMA 950, 342, `3ea75094` | pass (8) | pass (0) | **268 mismatches, 37 pixels outside the triangle** | **269 mismatches, 37 outside** |
| GMA 950, 344, with the fix below | pass | pass (0) | pass (8) | pass (4) |

(Worst channel error in brackets, tolerance 9.)

**The two-stage draw was half a pixel off, with or without specular.**
`v9x_d3d_draw_two_units` hands a two-stage DrawPrimitives2 batch to the
engine without the list builder, which is where a one-stage batch is
moved onto the engine's pixel centres. Gen3 samples at x + 0.5 and has
the core make that move, so its two-stage Direct3D draws landed half a
pixel up and left of every other draw: 37 pixels outside the probe's
triangle, the number an unmoved shader draw gave on boot 303. In the
tree since Gen3 got two stages (`746c0a42`, 2026-11-12, released in
0.49.0), not from today's work; found only because `FfSpec2` needed a control. The path now moves
the vertices in the copy it makes for culling. The Rage 128, the other
two-stage engine, makes the move itself and is not changed.

**The HD 4600, boot 262** (`3ea75094`): 3DMark 2001 SE, all 21 tests,
**2567**; Environment Bump Mapping 47.7 fps, DOT3 28.7 fps; no fault, no
shader triangle dropped. Recorded, not compared.

**The netbook, boot 344** (`3ea75094` plus the move, built dirty):

- probe set passes, no verdict changed; V9XD32 at DDI 8 PASS;
- 3DMark 2001 SE, all 17 tests: **985**; DOT3 5.8 fps, Environment Bump
  Mapping 10.0 fps, High Polygon Count 0.7 / 0.6;
- 3DMark 2000, default benchmark, 1024x576 16-bit: **1548**; Game 1
  29.7 / 22.7 / 8.7 fps, Game 2 28.9 / 22.7 / 16.3 fps, High Polygon
  Count 942 / 924 / 920 KTriangles/s, fill rate 70.7 / 141.4 MTexels/s;
- Half-Life 1.1.1.0, Direct3D, windowed, `timedemo mwd5`: 45.1 then
  55.9 fps (393 frames); the frame looks right
  (`half-life-d3d-netbook-boot344-frame.png`).

All recorded, not compared. After the two 3DMarks the driver's counters
show 853,648 two-unit batches, none declined, no draw refused by the
engine's builders, and two things not accounted for: 4 batches the
engine refused and 160 triangles its clipper dropped
(`BatchesEngineRefused`, `I9xxGuardDropped`). Whether either is new with
the move was not measured; boot 342 has no snapshot after a 3DMark 2000
run to set beside it.

**Boot 343**, the same build as 344: V9XD32 `/ddi5` hung once right
after the boot and was ended at the agent's 120 s limit; run again in
that boot, and in 344, it passed. Not explained. The reset out of that
boot ended on the blue screen filed as
[`../issues/2026-11-14-netbook-fatal-exception-0e-at-agent-reset.md`](../issues/2026-11-14-netbook-fatal-exception-0e-at-agent-reset.md),
which also has what the ended run left in SYSTEM.INI.

## Not done

- The DECAL one-unit form of the specular program judged pixel by
  pixel; and specular at DDI 6 or 7 judged at all (3DMark 2000 and
  Half-Life ran through it; nobody compared a frame with another
  card's).
- UT99 or another DirectX 7 game.
- The two counters above.
- Why 3DMark's 8-light test read N/A under the shader-path attempt.

Gates: `run-checks.ps1` passed on the tree with the move; the probe's
last row and its skip condition were built after and run on both
machines.
