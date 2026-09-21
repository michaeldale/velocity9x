# Intel 3DMark99: the missing textures, after intel102

Written 2026-09-21 from a review of
`docs/decisions/2026-09-20-intel102-the-draws-are-aimed-correctly-and-still-nothing-lands.md`
and the records it rests on (intel94 to intel101), the code they instrument,
the netbook's boot INI, and an online check of the gen3 reference trees.
Nothing here has run on the netbook. This plan states what the evidence
supports, what it does not, and the order in which to spend captures.

**Revised 2026-09-21**, after checking every code claim above against the
tree at `491042c` rather than against the review that produced them. Three
were wrong, and all three change a step:

- `GUID_D3DExtendedCaps` is **already answered**, and has been since
  `facf2f5`, which is an ancestor of the `8d4cf3c` build intel102 ran. Step
  6 is done; section 3's second bullet is withdrawn below.
- `V9X_I9XX_MAP_DIMENSION_MAX` is **already 2048**. Step 3 is smaller than
  it was written to be.
- The untextured program **cannot write a constant** without an encoding
  this driver does not have. Step 2 needs a different lever.

No netbook run has happened. Steps 1, 2 and 4 still need one, and step 3 is
still gated on step 1.

## 1. The premise of intel100 to intel102 conflicts with the photograph

The netbook photograph attached to intel98
(`2026-09-20-intel98-netbook-3dmark99.jpeg`) shows a near-white background,
a fully textured blue-and-orange rail, and a small grey untextured triangle
at lower right, at roughly x 780 to 890, y 429 to 462 in 1024x576
coordinates. intel99 records the operator saying the scene looked the same
after the depth fix.

intel100, intel101 and intel102 read every sampled buffer as one uniform
value (`0x0000` or `0x8410`), and both attached PPMs are 9,216 pixels of
literal `0,0,0` (checked byte by byte, 128x72 P6).

The CPU aperture (GMADR), the display plane and the 3D engine all address
stolen memory through the same GTT. A buffer the panel shows as white cannot
read black through the aperture at the same offset. So the readback and the
photograph cannot describe the same frame, and neither of intel102's two
candidates survives:

- a GTT-translated destination against the aperture view would break the
  display in the same way it breaks the readback;
- "the primitives produce no fragments" is refuted by the rail on screen.

Two physical escapes were checked and closed:

- **Stale CPU cache.** `docs/probe/intel-phase5/V9XBOOT-b1-318584c.ini`
  records the netbook's MTRRs: `Mtrr0` WB over 0 to 2 GB, `Mtrr1` UC over
  the 8 MB of stolen memory at `0x7F800000`, default type UC, GMADR at
  `0xD0000000`. The aperture is uncached. Reads are fresh.
- **Read before the engine drew.** Submission is synchronous: the submit
  spins until the head reaches the tail and then polls the breadcrumb
  (`BreadcrumbLagPollsTotal=0`, `BreadcrumbOutstanding=0`), and the recheck
  one flip later agreed in all four records.

What remains is the sampling window, and it is the whole story.

## 2. The records describe the first thirty seconds of the run

`src/display32/engines/i9xx_scanout.c`: the four cover slots fill once and
stop, sampling every `V9X_I9XX_COVER_INTERVAL` (64) flips to a non-zero
offset; the image is written once per session. In intel102 the records are
flip sequences 1, 98, 196 and 293 of 2,794, and `FrameImageSeq=1` is the
first flip after the mode switch. `FrameCoverRequest=0`: no capture was ever
armed. The tool has the verb (`V9XTRACE -arm`,
`tools/diag/d3d_trace_dump_win32.c:466`, raising `V9X_DDARMFRAME` in
`dd16.c:968`), and it has not been used.

So the three "black buffer" records characterise 3DMark99's opening, most
plausibly its loading and title screens, and call that the buffer. The
conclusions drawn from them - that nothing lands, that the flip chain is
black - are not about the game scene at all.

One clue cuts the other way and is recorded rather than dismissed:
`Cover1Box=776,432 864,456` lands within one 8-pixel sampling step of the
grey triangle in the photo. If sequence 98 was in-scene, the rail and the
white background were absent from memory while visible on the panel, which
is not physically coherent and would mean the instrument itself is wrong.
Either reading needs the same experiment (step 1 below) before another
cover record is interpreted.

## 3. A reading of the picture that fits every counter

The untextured fragment program (`src/chipsets/intel/i9xx_fragprog.c`) is
`dcl T8; mov oC, T8`: interpolated diffuse and nothing else.
`DrawsNoHandle=30140` is 17 per cent of draws submitted with
`TEXTUREHANDLE=0`, drawn diffuse-only. The sky, road and buildings are a few
large triangles; the rail is hundreds of small ones. "83 per cent textured"
is a count of draws, and is fully compatible with most of the screen AREA
being untextured white geometry on a white sky. The scene may be entirely
present and mostly white on white. VOGONS reports of 3DMark99 on a ViRGE
with missing capabilities describe exactly this shape: "monochrome
rendering, no textures", not blank screens
(https://www.vogons.org/viewtopic.php?t=39965).

Why the application would withhold textures from those objects:

- `d3d_i9xx.c:1200` advertises `D3DPTEXTURECAPS_SQUAREONLY | POW2` with a
  maximum of 256. The comment at line 1052 says the size range was
  generalised, not measured. Sky strips, road and building maps are commonly
  non-square or 512 wide. Mesa's `i915_texstate.c` encodes MS3 as
  `((h-1)<<21) | ((w-1)<<10) | format`, width and height independent, and
  xf86-video-intel's `i915_render.c` accepts both up to 2048. Square-only is
  this driver's rule, not the chip's. `D3dTextureRefusedShape=0` is
  consistent with it: an application told "square only" never asks.
- ~~The extended-caps GUID is still declined~~ **Withdrawn.** It is
  answered. `d3d_core.c:2432` returns `DDHAL_DRIVER_HANDLED` with the
  shared block's limits before the declining loop is reached, and
  `facf2f5` put it there ahead of the `8d4cf3c` build intel102 ran.
  `DriverInfoGuid09=0x7DE41F80` in the intel102 snapshot is that GUID's
  Data1, so the runtime did ask and did get an answer;
  `DriverInfoDeclined=14` of 18 counts the other GUIDs.

  This does not weaken the bullet above it - it **strengthens** it. The
  application was told, explicitly and in the one structure that can carry
  it, that this device takes textures from 8 to 256 and square. An
  application given that and asking for nothing outside it is exactly
  `D3dTextureRefusedShape=0`, and there is now no remaining reading in
  which the limits were published and unheard.
- 3DMark99 result dumps show it prefers `4444 RGBA` textures in 16-bit
  modes. The driver enumerates 565, 1555 and 4444 and records the two alpha
  types as unmeasured.

A residency squeeze is a second, independent factor: 7.69 MB of video
memory, three 1024x576x16 buffers plus Z take 4.5 MB, leaving about 3.3 MB
for textures (`0x480000` to `0x7B0000`). It cannot be separated from the
caps question by counters, but it can by a mode change.

## 4. Steps, in the order to spend captures

Each step is one netbook run. Do not start step 3 until step 1 has run.

1. **Arm a capture mid-scene and photograph the same moment.** Run
   3DMark99, and while the rail is on screen run the dump tool with `-arm`,
   then dump. Compare the new PPM against the photo. A PPM that shows the
   rail and a white field says the window was wrong and sections 1 and 2
   stand. A PPM that is still uniform says the instrument is wrong, and that
   becomes the only question. Record either as a decision doc.

   **The arm is not instantaneous, and the procedure has to allow for it.**
   `v9x_i9xx_note_frame_coverage` returns on the countdown before it looks
   at `frame_cover_request` (`i9xx_scanout.c:947` against `:1006`), so an
   `-arm` is not seen until the next 64-flip boundary and the captured
   frame is up to 64 flips later than the moment of arming. intel102's
   2,794 flips make that a few seconds. So: arm while the rail is on
   screen and with the scene still running, photograph the panel through
   the following seconds, and only then dump.
2. **Debug build: make the no-handle geometry magenta.** ~~Replace `mov oC,
   T8` with a constant magenta.~~ That cannot be written. A Gen3 fragment
   shader has no immediates, and this driver has neither a constant
   register type nor a `_3DSTATE_PIXEL_SHADER_CONSTANTS` packet -
   `intel_gen3_3d.h` declares `FS_REG_TYPE_R`, `_T`, `_S` and `_OC` and
   nothing else. The alternatives are a new swizzle selector (Mesa's i915
   spells ONE and ZERO as source selectors 4 and 5, which this tree's audit
   has not recorded) or a new packet; both are unaudited encoding claims,
   and the plan budgeted neither.

   Do it in the vertex colours instead. The untextured branch at
   `d3d_i9xx.c:1559` hands `colors` to `v9x_i9xx_build_runtime_run`;
   overwriting that array with magenta for that branch alone is a throwaway
   change to data the builder already carries, needs no new encoding and no
   audit, and puts exactly the same magenta on the panel. Whatever is
   magenta is the no-handle geometry. One photo, no counters. Not shipped,
   not gated: revert after the capture.
3. **Drop SQUAREONLY and raise the published texture maximum** to what the
   two reference trees license: independent width and height, POW2, up to
   2048. ~~Bounds in `d3d_i9xx_target.c` and the caps move together.~~
   `V9X_I9XX_MAP_DIMENSION_MAX` is already 2048 and `v9x_d3d_i9xx_bind_map`
   already checks width and height independently, so the target side does
   not move at all. What moves is in `d3d_i9xx.c`: `texture_size_max` in
   `v9x_d3d_i9xx_limits` (256), the `SQUAREONLY` bit in `dwTextureCaps`,
   and the `wWidth != wHeight` test in `v9x_d3d_i9xx_bind_texture`. The
   limits feed the extended caps, so raising them is what the application
   is now known to read. Host tests first. Measure with the same photo,
   `DrawsNoHandle`, and `D3dTextureCreates`.

   The 256 ceiling has a stated reason - a 2048-square map is 8 MiB and
   stolen memory is 8 - so raising it interacts with step 4 rather than
   being independent of it. Dropping SQUAREONLY alone, with the ceiling
   left at 256, is the smaller first move and tests the more likely half.
4. **Run at 640x480x16, or with a two-buffer chain**, with no code change.
   More textured objects at the smaller footprint means residency is part of
   the picture and the video-memory report to the application needs work.
5. **Record no-handle draws by screen area**, not count, only if steps 2
   to 4 leave the partition unexplained.
6. ~~**Answer `GUID_D3DExtendedCaps`.**~~ **Done, before this plan was
   written.** `facf2f5`, in the build intel102 ran. Nothing to do; see the
   revision note at the top for what its being answered implies.

## 5. What was looked for online and not found

- No 915/945 Programmer's Reference Manual Volume 3 is mirrored anywhere
  reachable; the gen3 3D reference remains Mesa's classic `i915` driver and
  xf86-video-intel's `i915_render.c`.
- No Win9x Direct3D driver for GMA 900/950 exists. The repackaged 945GM
  driver for Windows 98 is 2D only.
- JHRobotics `vmhal9x` (https://github.com/JHRobotics/vmhal9x) is a
  from-scratch Win9x D3D HAL over Mesa that runs 3DMark99, and the closest
  open reference for DX6 texture-handle semantics if step 2 or 3 points at
  the handle path.
