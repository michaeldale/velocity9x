# Three scalar fixes to the software rasterizer, and what the first attempt taught about calls

Date: 2026-09-10. Implemented and measured in existing 86Box guests only.
No physical machine ran this code; BARRY, which the plan's timing steps need,
has not answered ICMP since its 2026-09-06 hang.

Step 3 of [`software-rasterizer-scalar-fixes.md`](../plans/software-rasterizer-scalar-fixes.md):
fixes 2, 3 and 4 of that plan, landed together as the plan asks, because they
touch one loop body and each is bit-identical by construction.

Result on the Trio64 guest, RAM target: textured point sampling 1.45x,
bilinear 1.19x, depth-tested 1.18x, alpha-blended 1.17x, Gouraud 1.14x.
Every colour and depth hash in both guests is unchanged, and no entry in the
host pixel table moved.

## What changed

`src\display32\d3d\d3d_raster.c` only. Three changes, in the plan's numbering.

**Fix 2, exact division by 255.** The modulate arm formed
`(texel * channel + 127) / 255` three times per textured pixel.
`V9X_D3D_RASTER_DIV255` replaces each with a multiply by `0x8081` and a shift
of 23. The identity holds for every value from 0 to 66298 and breaks at
66299 - checked exhaustively, not derived - and the numerator is at most
`255 * 255 + 127`, which is 65152. A compile-time assert holds the two
numbers together, because a margin of a thousand is not something to leave to
a comment. The multiply is unsigned: the product 2,143,305,344 does fit in a
signed 32-bit integer, four million short of the limit, and that is too thin
a margin to hand the next caller.

**Fix 3, one clamp instead of three sites.** The loop clamped each colour
channel to 0..255 before modulate, again before the blend, and a third time
inside the packer. It now clamps once, immediately after the interpolator,
and the position is load-bearing: modulate multiplies by the channel, so the
clamp must precede the texture stage. With that guarantee the blend arm's
copy and the packer's are dead, so both are gone from the loop's path - the
two exported packers keep their guard for callers that are not this loop, and
call the unguarded arithmetic underneath. Each clamp is one unsigned compare
rather than two signed ones: a negative value converts to something far above
255, so a single test catches both ends. The usual `value >> 31` mask form is
not available, because C89 leaves the right shift of a negative signed value
implementation defined and this file already refuses to rely on that.

**Fix 4, per-pixel dispatch hoisted.** The depth comparison was a `switch` on
the D3DCMP function per pixel; it is now a three-bit relation mask resolved
once per span, and the mask *is* `D3DCMP - 1` - bit 0 nearer, bit 1 equal,
bit 2 farther - which the header's deliberate use of the D3DCMP numbering
licenses and a new assert holds. The pixel-format test likewise moves out: a
span resolves one pack and one unpack pointer, which also collapses the old
two-call chain (dispatcher, then packer) into one indirect call. Unrecognised
compare functions still resolve to ALWAYS rather than NEVER, for the reason
`v9x_d3d_z_compare` gives: a driver that renders black says nothing about
why.

The plan's option of specialised span loops generated from a macro body was
not taken, as the plan directs.

## What the first attempt measured, and why it is in this record

The first build expressed the clamp and the divide as small `static`
functions, which is what this tree's style asks for. Measured on the Trio64
guest against the same baseline, it made the textured scenes faster and the
untextured ones **slower**: RAM Gouraud 0.865x, RAM Small 0.949x, VRAM
Gouraud 0.870x. Point sampling still gained 1.30x.

The cause is that nothing here inlines. The HAL is compiled
`-bt=nt -bd -zq -wx -we -zl -s` and the benchmark executable matches it
deliberately; no `-o` option is passed, so a `static` helper is a real CALL on
every pixel. The untextured path had gained three calls and lost nothing,
and it had also gained a *second* clamp - the loop's own, on top of the
packer's, which the first attempt left in place.

Both are fixed in the shipped form: the clamp and the divide are macros
(`V9X_D3D_RASTER_CLAMP255`, which names its lvalue three times in the manner
of the existing `V9X_EDGE_NEXT`, and `V9X_D3D_RASTER_DIV255`, which evaluates
its argument once), and the packers are split so the clamp happens once.

This is recorded because the plan's cycle rankings assume an optimising
compiler's inlining and this build has none of it. Any later item in that
plan that adds a helper to the per-pixel path should expect to pay a call for
it and should be measured, not reasoned about. The `trio64-candidate-calls`
report and its CSV are kept as the evidence.

## Method

Same instrument and procedure as the
[2026-09-07 record](2026-09-07-software-rasterizer-edge-stepping.md), which
the probe README documents. Baseline is `c4988fe`'s `d3d_raster.c` - the
edge-stepping version, not the pre-edge one - captured with `git show` and
compiled into a separate executable, so both binaries differ in the
rasterizer alone.

- `Win98SE-Trio64`, loopback port 9871: 800x600x16, 4 MB Trio64, agent 0.5.2.
  RAM and VRAM targets both measured. Boots 334 and 335 - the table below is
  boot 335, where the baseline and the committed source ran against each
  other.
- `Win86SE`, loopback port 9869: ViRGE/DX, 1024x768x32, boot 573, agent
  0.6.0. RAM only - a 32-bpp desktop yields `VramAvailable=0`, as the
  instrument's README states.

Both guests are YM430TX with a 200 MHz Pentium MMX and 128 MB, dynarec
enabled. The guests ran one at a time, each shut down through the agent
before the next started; nothing else was benchmarking. Three samples per
workload of at least 750 guest milliseconds after a warm-up frame, median
reported. Drift, measured as the same baseline executable run twice on boot
334: within 0.7 per cent, and 0.7 per cent again on the ViRGE guest. That is
smaller than every gain claimed below except the memory sweeps, where nothing
is claimed.

Boot 334 measured the same rasterizer as boot 335 with three compile-time
asserts missing from the depth-mask check. Those emit no instructions, and
its figures - Point 1.46x, Gouraud 1.11x, Bilinear 1.19x - reproduce boot
335's within drift, which is also the only cross-boot repeatability check
here. The ViRGE guest ran that same pre-assert build.

Artefacts, including the rejected first candidate:
[`docs\probe\software-d3d-2026-09-10`](../probe/software-d3d-2026-09-10/README.md).

## Results

Trio64 guest, boot 335, median milliseconds per scene:

| Target | Scene | Baseline | Candidate | Speedup |
|---|---|---|---|---|
| RAM | Small | 19.359 | 18.537 | 1.04 |
| RAM | Gouraud | 20.132 | 17.674 | 1.14 |
| RAM | Point | 83.889 | 57.692 | 1.45 |
| RAM | Bilinear | 164.200 | 138.333 | 1.19 |
| RAM | Depth | 172.000 | 145.833 | 1.18 |
| RAM | Alpha | 206.250 | 177.000 | 1.17 |
| VRAM | Small | 20.132 | 19.359 | 1.04 |
| VRAM | Gouraud | 22.500 | 20.000 | 1.13 |
| VRAM | Point | 115.143 | 88.333 | 1.30 |
| VRAM | Bilinear | 281.667 | 255.000 | 1.11 |
| VRAM | Depth | 320.000 | 294.667 | 1.09 |
| VRAM | Alpha | 382.500 | 353.333 | 1.08 |

ViRGE guest, RAM only: Small 1.04x, Gouraud 1.14x, Point 1.48x, Bilinear
1.20x, Depth 1.20x, Alpha 1.19x.

The memory sweeps move by up to 4 per cent on boot 335's RAM read and under 1
per cent everywhere else, in both directions across the runs; this change
touches no memory access pattern and none is claimed for them.

## What the numbers dispute

**The plan's ranking put the divide behind the clamps and the dispatch.** It
is the other way round where it matters most: the scene that gains most is
point-sampled modulate at 1.45x, and it is the one carrying three divides.
The scenes that gain least - flat and Gouraud fills, 1.04x to 1.14x - are the
ones with no divide to remove, and what they got was the deleted duplicate
clamp.

**The gain shrinks on the VRAM target, consistently.** Point sampling gains
1.45x in RAM and 1.30x in video memory, bilinear 1.19x against 1.11x. The
aperture is absorbing part of every saving, which is the direction the plan's
non-scalar finding predicts - textures, depth buffer and blend destination
all live in video memory - and is the argument for measuring residency before
items 5 to 7. It is not a measurement of the aperture itself: this is 86Box's
model of one, and the physical read timing the plan's step 1 asks for is
still unrun.

**Nothing here says anything about a game.** These are synthetic scenes at
320x240 on an emulated Pentium MMX. A 1.2x on a scene that takes 137 ms is
still a scene that takes 114 ms.

## Limits

- Emulator only, both guests. No physical timing, no frame rate from a named
  application, no claim about either.
- The pixel-identity claim rests on the host table under Open Watcom plus 18
  guest colour/Z hashes. `build-host-msvc.ps1` did not run: this host has no
  Visual Studio, so `vswhere.exe` is absent and the second compiler's pass
  over the same table is owed.
- `run-checks.ps1` passed on the final source, so the HAL that carries this
  code builds for all four families. No family package was installed in a
  guest and no driver-level Direct3D run was made - the benchmark links the
  rasterizer directly and never loads the HAL.
