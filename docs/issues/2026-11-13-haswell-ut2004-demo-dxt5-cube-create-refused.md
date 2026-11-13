# HD 4600, UT2004 demo: a DXT5 cube map is refused at creation and the game stops at its first frame

Date: 2026-11-13. Seen on the i5-4570 (HD 4600, W9QI5A83), boots 231 and
234, on the HAL of `e9553ba8` over the boot-218 intel-gma set (main's
`746c0a42` code), DirectX 9.0c.

Status: **create fixed in source 2026-11-13, measured on the 4570 (boot
235); unaligned base offsets fixed in source and measured (boot 236);
MODULATE2X added, measured (boot 238); fixed-function cube stages drawn
through the shader path, measured (boot 241); floor holes from dropped
two-unit cuts fixed, measured frame for frame (boot 243)**. The benchmark
runs to its score, 31.7 to 33.5 fps, with no batch refused. Michael
watched boot 236's run and saw lighting or textures missing; that was
MODULATE2X (below). After boot 241 he played and reported the floor
sometimes see-through; that was the two-unit cut (below). Open: nobody
has judged whether the environment maps look right.

## The program

Unreal Tournament 2004 demo, build 3334 (`UT2004-NewDemo.exe`,
296,049,152 bytes, MD5 `91a884ceb079d0ba5d6b023b6cf76fc2`, from the
Internet Archive item `UnrealTournament2004Demo`), installed with its
defaults to `C:\UT2004Demo`. Its `UT2004.ini` as shipped: the Direct3D 8
renderer (`D3DDrv.D3DRenderDevice`), 800x600 full screen,
`UseHardwareTL=True`, `UseHardwareVS=True`, `UseStencil=False`,
`UseCompressedLightmaps=True`.

Run from `C:\UT2004Demo\System`:

```
UT2004.exe dm-rankin?spectatoronly=1?numbots=12?quickstart=1?attractcam=1
    -benchmark -seconds=77 -exec=..\Benchmark\Stuff\botmatchexec.txt -nosound
```

`botmatchexec.txt` is not in the demo; it holds the two console commands
`showhud` and `ship`.

## What happens

Twice in two runs, about 7 s after launch:

1. The game creates its device: 800x600, back buffer format 22
   (X8R8G8B8), depth format 77 (D24X8), at DDI 8 (`gdi2-dxversion
   00000802` in `V9XD7.LOG`).
2. It precaches DM-Rankin. The D3D8 proxy logs 2D textures in DXT1, DXT3,
   DXT5 and A8R8G8B8, 32x32 to 1024x1024 with full mip chains, every one
   created (`-> 00000000`). `V9XPS.LOG` shows them placed.
3. At the first frame, drawing a static mesh:
   `CreateCubeTexture 128 levels 8 usage 0 fmt 894720068 pool 1 ->
   8876086c`. The format is `35545844h`, DXT5; the pool is managed; the
   result is D3DERR_INVALIDCALL.
4. Unreal shows its own "Critical Error" dialog,
   `CreateCubeTexture failed(D3DERR_INVALIDCALL)`, with the history
   `FD3DTexture::Cache <- FD3DRenderInterface::CacheTexture <-
   FD3DRenderInterface::SetShaderMaterial <- ... <- RenderStaticMesh`.
   No frame is benchmarked and `Benchmark\Results` stays empty.

The game does not ask `CheckDeviceFormat` for a DXT5 cube in the proxy's
last 120 lines before the call; whether it asked earlier in the run was
not read.

## Cause

`v9x_d3d_get_driver_info2`'s GETFORMAT answer in
[d3d_core.c](../../src/display32/d3d/d3d_core.c) adds
`D3DFORMAT_OP_CUBETEXTURE` to every texture format where cubes are
claimed, except DXT1, DXT3 and DXT5: "whose cube layout nothing here has
measured". With `D3DPTEXTURECAPS_CUBEMAP` set and the operation absent
for DXT5, the runtime refuses the create itself. That the call never
reached the HAL is read from the log tails (no surface line follows the
last 2D texture), not from a trace of the create path.

The same proxy log holds 3DMark 2001 SE's earlier cubes on this machine,
`CreateCubeTexture 256 levels 1 fmt 21` (A8R8G8B8), all created.

## What is not known

- Whether a DXT cube's faces can be placed by the Haswell engine as the
  2D block trees are. Nothing has measured it.
- What follows the create. The comment at the cube caps in `d3d_core.c`
  says the fixed-function draw refuses a cube (step 5c of the DDI 8
  plan); UT2004's environment maps are fixed-function, so the next stop
  may be an undrawn mesh. Not run.
- Whether the game runs on the cards that claim no cube maps. Not run on
  any other machine.
- The OpenGL and Direct3D 9 renderers named in the demo's INI. Not run.

## The fix (2026-11-13, uncommitted at the time of the run)

- `d3d_core.c`: GETFORMAT now gives DXT1, DXT3 and DXT5 the cube
  operation wherever cubes are claimed, as every other texture format.
  Cubes are claimed only where the engine runs pixel shaders and DXT is
  listed only where it places DXT, so this reaches the Haswell engine
  and the Ivy Bridge parts that share it (not run on one).
- `hsw_3d.c`: `v9x_intel_hsw_cubetree_bc`, a DXT cube's layout. Each face
  is the block tree `v9x_intel_hsw_miptree_bc` lays out; faces are QPitch
  apart, QPitch being the uncompressed rule (h0 + h1 + 12 j) with the
  compressed j of 4, counted in rows of blocks; one level is the side in
  blocks. The rule is recalled from Mesa's `brw_tex_layout.c`, not read
  from the PRM. Host test `test_cubetree_bc`, written first and seen to
  fail (undefined symbol), checks UT2004's own shape: 128, 8 levels, 16
  bytes a block gives pitch 512, QPitch 60, 349 rows.
- `d3d_hsw.c`: `v9x_d3d_hsw_create_cube` lays a DXT cube out with it, and
  answers a DXT or signed cube with no room out of video memory, as a
  tree is.

Gates: `build-host.ps1` and `run-checks.ps1` passed. HAL only, by
WININIT rename (SHA-256 `5F274732...`, built from `bf18b412` plus this
change), over the boot-218 DRV and VxD; boot 235.

## Boot 235: the benchmark runs to its score

Same command line. The game exits by itself after 84 s;
`Benchmark\benchmark.log`:

```
6.163121 / 33.453346 / 84.887512 fps -- Score = 33.465759
```

(min / average / max fps on DM-Rankin, 800x600x32, 12 bots, no sound.)
The proxy logs four cube creates, all `-> 00000000`: DXT5 128 with 8
levels (twice), DXT5 64 with 7, and DXT1 (`fmt 827611204`) 128 with 8.
V9XTRACE after the run: `EngineIdleTimeouts=0`, `Dp2Undrawn=0`,
`Dp2RefusedBuffers=0`, `MipTreeDeclined=0`, 63,364 DP2 calls and
63,240,059 triangles.

Whether the cube faces are sampled where they were written is not
established: nothing has looked at a frame. A wrong QPitch would show as
the wrong face or a smeared reflection, not as a refusal.

## Still open 1: 11,462 batches refused, all reason 3

`BatchesEngineRefused=11462`, and the Haswell refusal table
(`M64PolicyNN`) holds them all in reason 3, `V9X_D3D_HSW_REFUSE_TEXTURE`.
Reason 3 has six sites in `d3d_hsw.c`. The likeliest are lines 1233 and
1250: the fixed-function kernels refuse a cube, and UT2004 now has four
cubes to draw with through fixed-function stages. That attribution is a
reading, not a measurement; the other four sites (no pitch, unknown
format, a size out of bounds, a DXT texture DirectDraw placed) are not
excluded. A refused batch is not drawn on this engine.

## Still open 2: 935 indexed draws dropped for an unaligned base

`V9XD7.LOG` gained 935 `dx8-base-offset` lines in this run: a
DRAWINDEXEDPRIMITIVE2 whose base vertex offset is negative and not a
whole number of vertices. The stride is 28 bytes in 918 of them (40, 48
and 60 in the rest), the offsets such as `FFFF236C`. `d3d_core.c`
(the DRAWINDEXEDPRIMITIVE2 case in the DDI 8 draw) logs and returns, so
the draw is not drawn and is not counted in `Dp2Undrawn`.

Such an offset still names exact addresses: vertex i is at
stream + base + i x stride. With base = -k x stride + r (0 <= r <
stride), that is (stream + r) + (i - k) x stride, a pointer r bytes in
and a base of -k vertices.

### Fixed in source 2026-11-13, measured at boot 236

`v9x_dp2_negative_base` (`d3d_dp2.c`) does that arithmetic, and the
DRAWINDEXEDPRIMITIVE2 case calls it for every negative base, whole
vertices or not; `v9x_dp2_draw` then bounds-checks each index as for any
negative base. Host test `negative_base_tests` (written first, seen to
fail on the missing symbol) covers -32 at stride 20, a whole-vertex
offset, UT2004's `FFFF236C` at 28 bytes (k 2,017, r 8), the most
negative offset, the refusals, and a draw through it, including an
index that falls before the stream. `build-host.ps1` and
`run-checks.ps1` passed.

HAL only, SHA-256 `0D1F373C...`, both fixes, by WININIT rename; boot
236, the same botmatch:

| | boot 235 | boot 236 |
|---|---|---|
| `dx8-base-offset` lines | 935 | 0 |
| `Dp2Malformed` | 0 | 0 |
| `Dp2Triangles` | 63,240,059 | 65,451,861 |
| `BatchesEngineRefused` (all reason 3) | 11,462 | 11,577 |
| min / avg / max fps | 6.2 / 33.5 / 84.9 | 5.3 / 33.2 / 151.8 |

2,211,802 more triangles reached the draw, 3.5 %, none rejected by the
bounds check. The refused batches rose by 115, consistent with some of
the recovered draws also carrying a cube; not established. One run each,
so the fps difference says nothing.

## Missing lighting: MODULATE2X (2026-11-13)

Michael watched the boot 236 run and reported missing lighting or
textures. The d3d8 proxy's per-state draw table for that run
(`d3d8-proxy-stage-keys-boot236.txt`, ops in D3DTOP numbers: 2
SELECTARG1, 4 MODULATE, 5 MODULATE2X) totals 579,296 draws:

| Share | Stage 0 | Stage 1 | What the HAL did |
|---|---|---|---|
| 72.9 % | MODULATE2X (texture x diffuse) | none | drew MODULATE: half the light (`Dp2Stage0Approximated` 273,602) |
| 9.6 % | MODULATE | none | exact |
| 7.4 % | SELECTARG1 (base) | MODULATE2X (lightmap) | pair refused by `v9x_d3d_state_stages`, so `v9x_d3d_describe_unit1` drew stage 0 alone: no lightmap |
| 4.0 % | MODULATE2X | MODULATE2X | the same |

The rest is under 3 % each. A frame grabbed at boot 237 shows the base
textures at even brightness, with no light or shadow on brick, pillar or
floor (left half of `lighting-before-after.png`).

### Fixed in source, measured at boot 238

- `d3d_internal.h`: `modulate2x`, appended to the engine limits; set on
  Haswell and Ivy Bridge only. Every other engine keeps the old answers
  (a doubled stage 1 refused and stage 0 drawn alone; a doubled stage 0
  drawn as MODULATE). Gen4 shares the stage mapping and would refuse
  the new op outright, which is why it is gated, not general.
- `r3d.h`: `V9X_R3D_COLOROP_MODULATE2X`, Direct3D only, beside ADD.
- `d3d_state.c`: `v9x_d3d_state_stages` takes the flag and maps
  MODULATE2X of texel and input to it on either stage;
  `v9x_d3d_state_stage0_modulate2x` names a doubled single stage.
- `d3d_core.c`: `v9x_d3d_describe_draw` states a doubled stage 0 in
  `texture.color_op` (zero otherwise: `draw` is uninitialised, and only
  two-unit draws read it elsewhere, over which `describe_unit1` writes);
  the stage is not counted as approximated; ValidateDevice answers one
  pass for it.
- `hsw_eu.c` / `intel_hsw_eu.h`: general colour op 4, MODULATE2X: per
  component a mul, then that added to itself saturated, so 2 C Ct clamps
  at one as D3D's does.
- `d3d_hsw_map.c`: the op in the two-unit mapping, and
  `v9x_d3d_hsw_modulate2x_stage` for one stage, taking its alpha as the
  DX5 blend the core expressed (MODULATE: texture alpha if any, else the
  vertex's; MODULATEALPHA: their product).

Host tests written first and seen to fail: `test_modulate2x`
(`test_d3d_state.c`), `test_modulate2x_stage` and new two-unit cases
(`test_d3d_hsw_map.c`), `test_modulate2x_generator` (instruction count
and encoding, `test_intel_hsw_eu.c`). `test_general_kernels_fit` predicted
overflow by a DECALALPHA rule; it now counts each kernel's instructions
from the generator's structure, agrees with the old rule on every case
it named, and checks the built size of every stage pair with fog and
specular. MODULATE2X beside DECALALPHA and fog, or in both stages with
fog and specular, exceeds the 160-dword slot and is refused (stage 0
drawn alone, as before). `build-host.ps1` and `run-checks.ps1` passed.

HAL only, SHA-256 `97DA5ED9...`, all three fixes; boot 238. Same
botmatch, counters after the run less those before (one short run
preceded it on the boot):

| | boot 236 | boot 238 |
|---|---|---|
| `TwoUnitBatches` | 2,508 | 67,875 |
| `Dp2Stage0Approximated` | 273,602 | 6,370 |
| `TwoUnitDeclined` (kernel would not fit) | 0 | 727 |
| `BatchesEngineRefused`, all reason 3 | 11,577 | 11,577 |
| `Dp2Calls` / `Dp2Triangles` | 63,364 / 65,451,861 | the same |
| min / avg / max fps | 5.3 / 33.2 / 151.8 | 5.4 / 33.4 / 151.3 |

`lighting-before-after.png`: left, boot 237 (no MODULATE2X), right,
boot 238, a frame of nearly the same moment: the pillar darkens toward
its foot, brick and ceiling are shaded, the floor grating takes warm
light. The camera is not at exactly the same place; the counters are
the measurement, the frames the confirmation. `frames-boot238.png` holds
four grabs from the run, at flips 250 apart.

## Still open: cube-mapped draws, measured to one site

A measurement HAL (boot 237, not committed) counted each of reason 3's
six sites in `d3d_hsw.c` in spare Matrox-only diagnostic fields
(`V9XSNA7-boot237-reason3-sites.INI`). Every refusal came from the
one-unit fixed-function path refusing a cube: 2,242 in that run, all
with a cube bound, format `0x188` (BC3, DXT5), DX5 op 1 (DECAL); none
from the two-unit site, a missing pitch, a format, a size or a
DirectDraw-placed DXT texture. These are UT2004's environment-mapped
surfaces, not drawn.

### Drawn through the shader path, measured at boot 241

The d3d8 proxy's draw table shows UT2004's cube draws (texture type 5 on
stage 0): 7,441 draws a botmatch, stage 0 SELECTARG1(TEXTURE) with the
runtime's reflection-vector coordinates (`TEXCOORDINDEX` 0x30000,
`TEXTURETRANSFORMFLAGS` COUNT3), clamped, stage 1 passing CURRENT; 814
more put the cube on stage 1, added to a stage 0 that selects the
texture factor.

The engines that run pixel shaders sample a cube there (step 5c of the
DDI 8 plan, measured in 3DMark 2001 SE's Nature) and their wide vertex
carries all of a set's coordinates. So rather than teach the
fixed-function kernels cubes and three coordinates:

- `d3d_ps.c`: `v9x_d3d_ps_fixed` writes fixed-function stages 0 and 1
  as a ps.1.1 program: SELECTARG1/2, MODULATE, MODULATE2X, MODULATE4X
  and ADD over DIFFUSE, CURRENT, TEXTURE and SPECULAR, each stage
  saturated, CURRENT the diffuse colour on stage 0, an alpha DISABLE the
  input's alpha, a disabled stage 0 the vertex colour, the specular
  colour added last. Anything else (the texture factor, a modified
  argument, another op) is 0 and the draw stays on the old path. Host
  test `fixed_tests` (written first, seen to fail on the missing symbol)
  parses each program and runs it in the reference interpreter against
  D3D's arithmetic, worked by hand.
- `d3d_core.c`: a DDI 8 draw with no shader bound, on an engine with
  `draw_shader`, whose stage 0 (or stage 1 on a two-stage engine) binds
  a cube, is drawn under that program (`v9x_d3d_ps_fixed_shader`); the
  programs are kept by their tokens in an eight-slot table of their own.
- `d3d_handles.h/.c`: the first build (boot 239) drew nothing new
  (`Ps11KernelsBuilt=0`). A measurement HAL (boot 240,
  `caps2-at-draw-boot240.txt`) logged each stage texture once with its
  DDSCAPS2 at the draw: all 178, the cubes among them, read 0. DDI 8
  textures are held copies whose `lpSurfMore` is cleared. So the handle
  entry now keeps the DDSCAPS2 CreateSurfaceEx saw (`caps2`, cleared
  when the handle names another surface; host-tested), and the cube
  test reads that.

`build-host.ps1` and `run-checks.ps1` passed. HAL only, SHA-256
`CC5ED8FE...`, all four fixes; boot 241, the same botmatch after 180 s
of uptime:

| | boot 238 | boot 241 |
|---|---|---|
| `BatchesEngineRefused` | 11,577 (all reason 3) | 0 |
| `Ps11KernelsBuilt` / refused / triangles dropped | 0 / 0 / 0 | 2 / 0 / 0 |
| `TwoUnitBatches`, `Dp2Triangles` | 67,875, 65,451,861 | the same |
| min / avg / max fps | 5.4 / 33.4 / 151.3 | 5.7 / 33.1 / 153.1 |

`frames-boot241.png`: eight grabs from a second run (31.9 fps with the
grabs' stalls), 150 flips apart. No corruption, missing geometry or
stray colour in any of them. Which surfaces in them are the
environment-mapped ones has not been identified, so whether the
reflections are right is not established. The 814 stage-1 cube draws do
not translate (the texture factor) and take the old path: that pair is
refused and stage 0 drawn alone, so their cube is not drawn.

## See-through floor: two-unit triangles dropped at the cut

After boot 241 Michael played a deathmatch on the machine and
photographed the screen: patches of floor missing with straight edges,
older frame content showing through. No counter moved
(`BatchesEngineRefused` 0, `Dp2Undrawn` 0).

`v9x_d3d_hsw_draw_batch` sorts each triangle by `v9x_d3d_hsw_guard`: one
whose screen span reaches 8K must be cut to the target before the engine
draws it (PRM Vol 7, "Vertex XY Clamping and Quantization"; the
2026-10-25 guard band record). The one-unit triangle was cut; a
two-unit one was dropped, uncounted, on the reasoning that the render
interface clips its two-unit draws first. Since MODULATE2X (boot 238)
Direct3D sends two-unit draws too, unclipped inside the published
+/-8191 band: UT2004's lightmapped floors, whose near triangles span 8K.
Before boot 238 those were drawn one-unit, and cut.

### Fixed in source, measured at boot 243

- `r3d_clip.c`: `v9x_r3d_clip_triangle_uv1` cuts a triangle with a
  second unit's coordinates: the one-unit clip run twice, the second
  time with the second pair in tu tv; the cuts depend only on the
  positions, so the polygons match vertex for vertex. Host test
  `test_cut_second_unit` (written first, seen to fail): the cut's tu1 tv1
  perspective-correct, 3.5 and 4 at rhw 1 and 3, every vertex equal to
  the one-unit clip's.
- `d3d_hsw.c`: the two-unit triangle that needs the cut is cut and
  drawn as its fan, each vertex with its second pair.

`build-host.ps1` and `run-checks.ps1` passed. HAL only, SHA-256
`EC4723B0...`, all five fixes. Boot 243 repeats boot 241's sequence (a
reset, a clean botmatch, then eight grabs 150 flips apart) with the
game's INIs at the install defaults, so the same seed draws the same
frames at the same flips (`GrabSeq0` 2461 both times):
`floor-holes-before-after-0-3.png` and `-4-7.png`, boot 241 left,
boot 243 right. Frame 0 shows the near walkway missing and the room
below through it at boot 241, planks at boot 243; frames 3, 4 and 6
have smaller holes at the bottom edge, gone at boot 243. Nothing else in
the eight pairs differs that could be seen. The clean run: 32.9 fps, 0
batches refused, no triangles dropped by the shader path.

Michael's play saved settings the demo detected on its first
interactive run: 32 MB of video memory, so a 16-bit back buffer and Z16,
lower texture detail, coronas off. Runs since then had another seed
(24275), ~39 fps and 65-69 s. For the A/B his `UT2004.ini` and
`User.ini` were set aside and the install's put back; his were restored
after, hash-checked. One grab run under his settings (boot 243,
`frames-boot243-16bit.png`): 38.8 fps, 0 refused, no holes in the eight
frames.

## The GMA 950 netbook, boot 328

The same demo on W9QI2BE2 (HP Mini 110, Atom N280, 945GSE), released
0.49.0's DRV and VxD with this HAL alone (SHA-256 `EC4723B0...`, the
code of `008436c8`; no shared-ABI, DRV or mini-VDD change since 0.49.0),
by WININIT rename and `reset`. Installed with its defaults (Michael
accepted the EULA in chat), `UT2004.ini` changed only to 640x480 in
both client sections, since this video BIOS has no 800x600 mode. The
same botmatch, started 4.6 minutes after the reset:

```
1.869585 / 4.258847 / 33.482544 fps -- Score = 4.276170   rand[11503]
```

640x480, 32-bit back buffer, D24X8. It ran to its end and exited
cleanly (`appRequestExit(0)`) after about 10 minutes of wall time. V9XTRACE after it
(`netbook-V9XSNA7-boot328.INI`): `BatchesEngineRefused` 0,
`I9xxDrawsRefused` 0, `EngineIdleTimeouts` 0, `Dp2Undrawn` 0,
`Dp2Malformed` 0; 74,306 DP2 calls, 65,395,277 triangles (the HD 4600's
run: 63,364 and 65,451,861); `I9xxGuardCut` 243,074, all of them
one-unit triangles. (This record said here that Gen3 cut both units. It
did not: Gen3 dropped a two-unit triangle past its band, as Haswell
did, and it showed once Gen3 drew MODULATE2X pairs; see below.)
`MipTreeDeclined` 26,730; `D3dTextureRefusedBounds` 24.

What the counters say is not yet right on Gen3:

- No DXT: Gen3's limits stop before `dxt_textures`, so DDI 8 offers
  none and UT2004 ran on uncompressed textures.
- No MODULATE2X: `Dp2Stage0Approximated` 273,598 and `TwoUnitBatches`
  2,508, as on the HD 4600 before boot 238. Vertex lighting at half and
  lightmaps dropped; `netbook-frame-boot328.png`, one frame the agent's
  screenshot caught mid-run, is evenly lit.
- The cube-map shader route did not run: `Ps11Fogged` 0, where the HD
  4600 counted 12,014 fogged shader triangles in the same botmatch.
  Gen3 claims cubes and refused nothing, so its cube draws went another
  way, probably sampled as a 2D texture. Not measured.

The 4.3 fps is one run on an Atom whose runtime does the transform and
lighting in software; nothing here says where the time goes.

### Gen3 MODULATE2X, boots 329 and 330

Two parts (uncommitted at the time):

- **Two-unit draws:** `V9X_I9XX_COMBINE_MODULATE2X` (6, the render
  interface's COLOROP 6) in `i9xx_multitex.c`: `mul R2.xyz, Cp, Rt;
  add_sat R2.xyz, R2, R2`. The two-unit decoder licenses it unchanged.
  Host tests in `test_two_unit_stream` (written first, seen to fail):
  the encodings on either unit, and all 36 pairings decode with and
  without fog.
- **One stage:** Gen3's one-unit programs are linked into I9XXCODE,
  which has no room left, so a doubled stage 0 drawn alone goes to the
  shader path as `v9x_d3d_ps_fixed`'s program
  (`v9x_d3d_context_stage0_shader2x`). `modulate2x` is now two bits:
  `V9X_D3D_MODULATE2X_ONE` and `_TWO`; Haswell and Ivy Bridge set both,
  Gen3 `_TWO` only.

`build-host.ps1` and `run-checks.ps1` passed. HAL only by WININIT
rename over 0.49.0's DRV and VxD. Boot 329 runs both parts (SHA-256
`EB420E77...`). Boot 330 runs a measurement HAL with the one-stage
route switched off, so the two-unit part alone (SHA-256
`0C859885...`). Same botmatch, 640x480x32, after 180 s of uptime:

| | boot 328 (before) | boot 330 (two-unit only) | boot 329 (both) |
|---|---|---|---|
| min / avg / max fps | 1.87 / 4.26 / 33.5 | 1.85 / 4.35 / 33.2 | 1.29 / 2.74 / 21.2 |
| `TwoUnitBatches` | 2,508 | 68,739 | 68,739 |
| `Dp2Stage0Approximated` | 273,598 | 273,598 | 124,332 |
| `Ps11Fogged` | 0 | 0 | 921,624 |
| `BatchesEngineRefused` | 0 | 19,913 | 19,913 |
| `I9xxDrawsRefused` | 0 | 18 | 20 |

Readings:

- The two-unit part costs nothing measurable and draws the lightmaps:
  68,739 two-unit batches where 2,508 were. `netbook-frame-boot329.png`
  shows shading where boot 328's frame was evenly lit.
- The one-stage shader route costs 37 % of the frame rate on this
  Atom, and takes only about half of the doubled stages (124,332 still
  approximated, cause not looked at).
- The 19,913 refused batches come with the two-unit part, not the
  shader route: the same count with the route off. They are two-unit
  pairs `v9x_d3d_i9xx_accepts` rejects - before, the same pairs were
  refused by the core and drawn as stage 0 alone. Which test rejects
  them, and what a refused batch draws on Gen3 now, is not measured.

Michael ran the game on the netbook and judged it too slow to play;
the features are kept. The netbook was left on the boot 330 measurement
HAL.

### No regression against 0.49.0, boots 331-332

Michael asked that whatever would be a regression in the next release
be fixed. Three were:

1. **Two-unit triangles dropped at Gen3's band.** The 19,913 refused
   batches were `v9x_d3d_i9xx_draw_classified` dropping every two-unit
   triangle past Gen3's guard band, uncut: `I9xxGuardDropped` 149,488 in
   boots 329 and 330, 0 at boot 328. The HD 4600's defect, on Gen3;
   the record's note at boot 328 that Gen3 cut both units was wrong. It
   now cuts them with `v9x_r3d_clip_triangle_uv1`, as Haswell does
   since boot 243, and draws the fan with each corner's second pair.
2. **The one-stage shader route.** It cost 37 % on the Atom. Removed:
   a doubled stage 0 drawn alone is drawn as MODULATE on Gen3 again, as
   in 0.49.0. Native doubled one-unit programs, built only where the
   model is flat as the two-unit program is, are the way to draw it
   right; not started.
3. **Two-unit draws whose texture will not bind.** Boot 331, with 1 and
   2 fixed, still refused 16 batches (22 draws, `I9xxRefuseLast` 4):
   pairs whose base texture fails the bind (`D3dTextureRefusedBounds`,
   24 in every run) and which 0.49.0 drew untextured as stage 0 alone.
   A Direct3D batch has no CPU fallback, so refused meant undrawn. Gen3
   now draws such a Direct3D draw with stage 0 alone, as the core draws
   a pair the engine declines; an explicit (render interface) draw is
   still refused.

`run-checks.ps1` passed after each. HAL only over 0.49.0's DRV and VxD,
the same botmatch after 180 s of uptime:

| | boot 328 (0.49.0's behaviour) | boot 331 (1, 2) | boot 332 (1-3) |
|---|---|---|---|
| min / avg / max fps | 1.87 / 4.26 / 33.5 | 1.84 / 4.23 / 32.0 | 1.83 / 4.23 / 32.2 |
| `BatchesEngineRefused` | 0 | 16 | 0 |
| `I9xxDrawsRefused` | 0 | 22 | 0 |
| `I9xxGuardCut` / `I9xxGuardDropped` | 243,074 / 0 | 243,060 / 0 | 243,060 / 0 |
| `TwoUnitBatches` | 2,508 | 68,739 | 68,739 |
| `Dp2Stage0Approximated` | 273,598 | 273,598 | 273,598 |

Boot 332's HAL is SHA-256 `0DD5BC1F...`; the netbook is left on it.
The difference from 0.49.0 is the 66,231 lightmap batches now drawn on
two units, at no measured cost (one run each).

The HD 4600 ran the build of boot 331 (SHA-256 `F802E8F5...`, the same
code bar Gen3's change 3) at boot 244 with the install-default INIs:
32.8 fps against boot 243's 32.9, every counter equal (`TwoUnitBatches`
67,875, `TwoUnitDeclined` 727, `Dp2Stage0Approximated` 6,370,
`BatchesEngineRefused` 0); the split of `modulate2x` into its two bits
changed nothing there. Michael's 16-bit INIs were put back after.

## Runs that end at 30 s

Two runs (boots 237 and 238, the first after each reset) ended after
30 s with a score of 170 fps and a different game seed. Each logged
`Viewport WindowsViewport: WM_DisplayChange` after precaching, which no
85-second run did. Both started 10-12 s after the agent did; the good
ones 22-25 s. The runner now waits until the machine has been up 180 s,
and the two runs since completed. What changes the display that early
is not known.

## Evidence

[docs/probe/hsw-ut2004-demo-2026-11-13](../probe/hsw-ut2004-demo-2026-11-13/):
`UT2004-boot231.log` (the game's log of the first run) and
`d3d8-proxy-tail-boot234.txt` (the last 120 lines of
`C:\V9XDIAG\D3D8LG3.TXT` after the second run: device creation, the
textures and the refused cube). From boot 235: `benchmark-boot235.log`
and `avgfps-boot235.log` (the game's results), `UT2004-boot235.log`,
`d3d8-proxy-cubes-boot235.txt` (the device and the four cube creates)
and `V9XSNA7-boot235.INI` (V9XTRACE after the run). From boot 236:
`benchmark-boot236.log`, `UT2004-boot236.log`, `V9XSNA7-boot236.INI` and
`d3d8-proxy-stage-keys-boot236.txt`. Boot 237:
`V9XSNA7-boot237-reason3-sites.INI`. Boot 238: `benchmark-boot238.log`,
`V9XSNA7-boot238-run6-short.INI` (the 30-second run),
`V9XSNA7-boot238-run7.INI` (the full run, cumulative over the boot),
`frames-boot238.png` and `lighting-before-after.png`. Boot 240:
`caps2-at-draw-boot240.txt`. Boot 241: `benchmark-boot241.log`,
`V9XSNA7-boot241.INI` and `frames-boot241.png`. The netbook: boot 328
`netbook-benchmark-boot328.log`, `netbook-V9XSNA7-boot328.INI`,
`netbook-UT2004-boot328.log`, `netbook-frame-boot328.png`; boot 329
`netbook-benchmark-boot329.log`, `netbook-V9XSNA7-boot329.INI`,
`netbook-frame-boot329.png`; boot 330 `netbook-benchmark-boot330.log`,
`netbook-V9XSNA7-boot330.INI`; boots 331 and 332
`netbook-benchmark-boot33N.log`, `netbook-V9XSNA7-boot33N.INI`. The HD
4600's boot 244: `benchmark-boot244.log`, `V9XSNA7-boot244.INI`.
Boot 243:
`benchmark-boot243.log`, `V9XSNA7-boot243.INI` (after the grab run),
`floor-holes-before-after-0-3.png`, `floor-holes-before-after-4-7.png`
and `frames-boot243-16bit.png`.

The D3D8 logging proxy (`d3d8log3`, measurement only) and the real
`d3d8.dll` as `D3DR.DLL` are in `C:\UT2004Demo\System` on the machine.
