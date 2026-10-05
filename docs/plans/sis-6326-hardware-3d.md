# SiS 6326: hardware Direct3D

Date: 2026-10-05

Status: approved 2026-10-05 as written. Phase 1 done the same day
([first triangle](../decisions/2026-10-05-sis6326-3d-first-triangle.md)):
TDRAWDIR is 1 when the middle vertex is left of the long edge; the engine
samples at integer coordinates (Direct3D's centre) and owns bottom/right
ties, which a small up-left vertex shift turns into Direct3D's top-left
rule; 3D runs with the Turbo Queue off. Phase 2 done the same day
([shading and depth](../decisions/2026-10-05-sis6326-3d-shading-and-depth.md)):
a 1/256 shift suffices (2^-16 resolved); Gouraud is prestepped in y but not
x; Z16 is z x 2^15 and wraps to 0 at z = 1.0, so the driver clamps z; alpha
test, SRCALPHA blending and saturating additive work. Phase 3 done the
same day ([textures](../decisions/2026-10-05-sis6326-3d-textures.md)):
the pitch field is a float, (2m + 1) << (e + 2) bytes; U/V normalised,
W = RHW; all five D3D formats exact; wrap/mirror/clamp and bilinear are
Direct3D's; blend modes 0, 2, 4, 8 and 12 map DECAL, MODULATE, DECALALPHA,
DECALMASK and MODULATEMASK; mips are per pixel with quarter-step blending;
8A38h D4 must be pulsed (held with level field 0, it hung the engine
until a reboot). Phase 4 under way: `d3d_sis6326.c` and its host-tested
mapping are in. The first V9XDDP run (boot 212) matched SiS's HAL on every
untextured check, refused every textured draw (a mapping bug, fixed), and
hard-locked the machine at a depth fill
([issue](../issues/2026-10-05-a8u4i5-hard-lock-under-sis-d3d-v9xddp.md)).
Found the same day
([engine record](../decisions/2026-10-05-sis6326-d3d-engine.md)): an
untextured batch stalls the next textured one, and the "lock" was a Lock
retry loop on the stuck engine. With untextured draws drawn textured (Cpix),
V9XDDP passes 99 checks SiS's HAL passes, plus depth; specular, fog,
flip-pixel, ramp and sprite-with-Z remain. The Z test compares 15 bits
([record](../decisions/2026-10-05-sis6326-z-compares-15-bits.md)), so
depth fills are halved; V9XDDP's raw Z readback check now fails by design,
and its sprite and ramp cells inherit that. Page flips now program the
start address (CR0D/CR0C/SR27 D[3:0]); 20 flips take 328 ms, and
FlipPixelOk's 0 is its known GDI-page blind spot
([record](../decisions/2026-10-05-sis6326-page-flips.md)). Vertex fog and
specular measured and published
([record](../decisions/2026-10-05-sis6326-fog-specular.md)): V9XDDP now
fails nothing SiS's HAL passes, and passes three it fails. **Phase 4 done.**
Phase 5 started: Final Reality stalls the engine on its first Z-tested
batch (256x256 texture), reproduced by the probe but not yet explained
([issue](../issues/2026-10-05-a8u4i5-sis-3d-stalls-in-final-reality.md)).
SiS's own driver renders the scene with the same state; none of its
register differences, the Turbo Queue included, stops the replayed stall.
TEND after every triangle does: Final Reality's full benchmark renders
with no timeout ([record](../decisions/2026-10-05-sis6326-tend-after-each-triangle.md)).
Its 17,013 refused batches were all a mip magnification filter, now
folded: the benchmark runs with no refusal and no timeout
([record](../decisions/2026-10-05-sis6326-mag-filter-fold.md)). 3DMark 99
Max ran its full suite the same way
([evidence](../probe/a8u4i5-sis6326-3dmark99-2026-10-05/README.md)).
Half-Life remains.
V9XDDP's `D3DZ*Hr=88760231h` is its not-run marker, so "SiS's Z tests fail"
above means they did not run.

Target: SiS 6326 card 2 (rev 0Bh, 4 MiB SGRAM) in A8U4I5, running the `sis`
family with the 2D engine on DirectDraw
([engine record](../decisions/2026-10-05-sis6326-engine-under-directdraw.md)).
Follows [sis-6326-family.md](sis-6326-family.md), "Later".

## Why this chip is a good 3D target

Unlike the Rage IIC, the 6326 has a triangle **setup engine** that takes
Direct3D's own transformed vertices. The rasteriser takes IEEE floats for
X, Y, Z, U, V and W, in pixels and texture coordinates, with packed 8-bit
colour. The driver sorts the three vertices by Y, names the order in one
register, and writes them. There is no CPU edge setup, the work d3d_rage2.c
has to do.

## What is already measured

From SiS's own HAL, caught live (A8U4I5 boot 207,
[record](../decisions/2026-10-04-sis6326-3d-state-under-sis-hal.md)):

- Vertex X/Y/U/V are IEEE singles, unscaled.
- 89F8h names top, middle and bottom by vertex, plus the shading mode and
  the fire position. SiS fires on the write of TSWc and never writes TFIRE.
- RGB565 destination format 11h, the blend nibble order, the 13-bit
  inclusive clip packing and texel code 53h (ARGB4444) are as the datasheet
  says.
- 3D is on (SR39 D2) only while a Direct3D client holds a 16 bpp mode, and
  SiS gives the Turbo Queue 28K of 32K to 3D while it does.
- SiS's HAL renders 16 bpp targets only, with 16-bit Z. It passes V9XDDP's
  triangle, shading, fog, blend, texture-format, filter and mip checks,
  which makes it the reference for every phase below. Its own Z-buffer
  tests fail (88760231h).

From our own work: the 2D engine and its enable, the register reference
([sis6326-registers.md](../specifications/sis6326-registers.md) sections
6-8), and DirectDraw's full mode list.

## Shape

The same split as the 2D engine:

- `src/chipsets/sis/sis6326_3d.c`, host-tested: vertex sort and 89F8h
  encoding, the state words (enable, Z, alpha, destination, blend, fog,
  clip, texture set and blend, pitches, sizes), each against the datasheet
  and SiS's captured values.
- A write probe, `SIS3D.EXE` + VxD in the shape of `SIS2D`: it fires
  triangles into guarded off-screen targets and reads them back, and runs
  before any driver code.
- `src/display32/d3d/d3d_sis6326.c` on the neutral `draw` entry
  (`V9X_D3D_ENGINE_OPS`, the r3d draw description), selected by
  `d3d_select.c` for `SIS_6326`, with caps published only for what a phase
  has measured.
- The 16-bit side claims `CAP_D3D` for the chip only once the probe phases
  pass.

## Phases

1. **First triangle (probe).** Turn on SR39 D2, program one RGB565
   destination off-screen, and fire one flat triangle with every optional
   feature off. Settles:
   - the TDRAWDIR rule (one sample so far);
   - whether edges are inclusive;
   - the top-left fill convention against Direct3D's;
   - whether 3D runs with the Turbo Queue off.
2. **Shading and depth (probe).** Gouraud, Z16 test and write (format,
   pitch unit, compare order), alpha test, the blend factors Direct3D
   applications use.
3. **Textures (probe).** RGB565, ARGB1555 and ARGB4444 first. Then the
   texture pitch unit (unresolved: SiS's HAL wrote 280h/200h for a
   128-byte level), nearest and bilinear filtering, wrap/clamp,
   perspective with W, then mip levels.
4. **Driver engine.** `sis6326_3d.c` from the probe-proven encodings,
   `d3d_sis6326.c`, caps from phases 1-3, the 2D/3D handover (one hardware
   queue, so a 2D blit after a 3D draw needs no flush beyond the idle wait).
   Exit: V9XDDP's triangle, shading and texture battery matching SiS's HAL
   on the same card.
5. **Applications.** Final Reality, Half-Life and 3DMark 99 run for
   correctness. Scores are recorded if a run produces them, never chased.

## Decisions wanted

1. **Probe first, as for 2D.** Phases 1-3 are tools, not driver code, and
   each one is a decision record before anything ships. Recommended.
2. **16 bpp only**, as SiS's HAL. Recommended: the destination formats
   exist for 15 and 16 bpp, and the 8 and 24 bpp desktops keep software
   Direct3D.
3. **Turbo Queue off for 3D too**, unless phase 1 shows 3D needs it. SiS
   ran it on; our 2D runs without it. Recommended.
4. **Card 1 (rev C3) stays parked.** It locks this machine under SiS's own
   driver, so no 3D work runs on it.

## Safety contract

- Every probe write goes to off-screen VRAM inside guards; nothing scans
  out.
- Every wait is bounded; a stuck engine is a recorded timeout, not a spin.
- SR39 and the other sequencer state the probe changes are written back.
- A8U4I5 hard-locked on Half-Life's additive sprites under the Rage IIC
  (open issue, 2026-10-03). The phase 5 application runs therefore come
  after the probe phases, one application at a time.

## Open questions only hardware answers

- The TDRAWDIR rule.
- The texture pitch unit.
- Whether W is RHW.
- Enable bit 15 (set by SiS's HAL, reserved in the datasheet).
- What the odd member of each texture-blend mode pair does.
- Why SiS's own Z tests fail.
