# Mode 2 scalar fixes: make the rasterizer cheap before making it wide

Status: 2026-09-06. Planning only. Nothing here has been built or measured.
Cycle figures are Pentium P5 instruction timings from Intel's databook,
quoted to rank the work, not measurements of this code. The rasterizer has
never been timed on any machine
([parent plan](s3-trio64-voodoo2-hybrid-3d.md), mode 2, step 1 unrun).

Parent: [`s3-trio64-voodoo2-hybrid-3d.md`](s3-trio64-voodoo2-hybrid-3d.md),
mode 2. Sibling: [`software-d3d-smp-workers.md`](software-d3d-smp-workers.md),
which should not start until this plan and the parent's step 1 are done,
because every change here shrinks the work a second CPU or a vector unit
would be asked to share.

## Why scalar first

Three reasons, in order of weight.

1. **It reaches every target.** BARRY is a classic Pentium with no MMX, the
   VLB machine is a 486. Neither gets a second CPU or a vector unit, and
   they are the machines mode 2 was written for.
2. **It is held to a pixel table.** `tests\host\test_d3d_raster.c` already
   states what every table entry must produce. A change that draws the
   same pixels faster is verifiable on the host with no guest and no
   hardware. Nothing else in the mode 2 roadmap has that property.
3. **It changes what the later work measures.** A vector unit or a second
   core sharing a loop that spends half its time in integer division
   reports a gain that disappears when the division goes. The order
   matters for the record as much as for the speed.

## What the loop does today

Per triangle (`d3d_raster.c:806`): sort three vertices, walk rows.

Per row (`d3d_raster.c:881`): two calls to `v9x_d3d_raster_edge_at`
(`:471`), each running eight `v9x_d3d_raster_lerp` calls (`:88`). Each lerp
performs a division, a modulo, and a second division. That is up to 48
integer divisions per row before a span is set up.

Per span (`d3d_raster.c:522`): seven divisions to derive the column steps
(`:569`).

Per pixel (`d3d_raster.c:618`): a depth clamp and a switch on the compare
function (`:211`); a texture coordinate clamp or wrap by branches; a sample
that multiplies both coordinates by the texture size (`:429`) and decodes
one or four texels channel by channel; modulate with three divisions by 255
(`:724`); channel clamps before modulate (`:706`), before blend (`:746`) and
again inside pack (`:118`); a format test in pack (`:176`) and, when
blending, another in unpack (`:187`); a 16-bit store (`:789`).

On a P5 an integer divide is about 46 cycles and a mispredicted branch
about 4. A textured, depth-tested, modulated pixel therefore carries three
divides and something like a dozen branches, and a ten-row triangle spends
more cycles in its edge setup than in its pixels.

## The fixes, ranked

Each names what changes, why the obvious alternative is wrong, and how the
table holds it.

### 1. Incremental edges and per-triangle gradients

Compute each edge's eight slopes once, in the same fixed point the lerp
produces, and step them by addition per row. Compute the seven column
gradients once per triangle: under linear interpolation every attribute is
affine in screen space, so the step per column is the same on every span,
and the seven divisions at `:569` move from per span to per triangle.

The lerp's floor semantics are the risk. `v9x_d3d_raster_lerp` divides
before it multiplies so that a wide texture coordinate cannot overflow, and
it floors toward negative infinity deliberately. An incremental edge
accumulates rounding error across rows, so the row start must still be
placed by an exact evaluation or by a Bresenham-style remainder carried
alongside the slope. The shared-edge test
(`test_shared_edge_is_covered_exactly_once`) and the coordinate-refusal
test are the ones most likely to move; a pixel that differs is a bug in
the new stepping, not a tolerance to widen.

Removes: up to 55 divisions per row. Largest single item, and the one that
helps small triangles most.

### 2. Exact division by 255

For every `t` in 0..65535, `(t * 0x8081) >> 23` equals `t / 255`. The
modulate products at `:724-726` are at most 255 * 255 + 127, inside that
range. The replacement is bit-identical, so no table entry may change and
the test run is the proof.

Removes: three divisions per textured modulated pixel.

### 3. One clamp, branchless

The colour interpolants are lerps between endpoints already in 0..255 and
can leave that range only by a one-step rounding drift at the far end of a
span. Clamp once, in pack, and clamp with masks rather than compare and
branch. The clamps at `:706` and `:746` go. The depth clamp before the
compare stays, because a depth interpolant is compared against stored
values, not merely packed, and a drifted value must not pass a test it
should fail.

The table already contains Gouraud triangles whose corners hit 0 and 255;
those entries are the check.

Removes: six to twelve branches per pixel.

### 4. Hoist the per-pixel dispatch

Pixel format, texture format and depth compare are constant across a
triangle and are tested per pixel. Two options, and the second is the one
to take:

- A per-triangle selection among specialised span loops generated from one
  macro body. Faster, but the C89 tree gains a macro that is hard to read
  and easy to get subtly different across instances.
- Turn the depth switch into a three-bit less/equal/greater mask tested
  with one shift, and keep the format tests but move them out of the
  helper into a per-span pointer to the pack and unpack routine.

Take the second. It keeps one loop body, which is what the table is
written against. Revisit the first only if a measurement says the
remaining tests matter.

Removes: three or four tests per pixel.

### 5. Texel-unit texture coordinates

Fold the texture size into the interpolant at span setup so the sampler
stops multiplying by it per pixel (`:429`). Under WRAP the per-pixel range
branches then become one unconditional mask, which is safe here because
the engine has already shifted every coordinate non-negative before the
rasterizer sees it. CLAMP keeps a branchless min and max.

The wrap test entries from the 2026-09-02 record hold this.

Removes: two multiplies and several branches per textured pixel.

### 6. Cheaper bilinear

Lerp the two rows horizontally, then once vertically: nine multiplies
against the twelve at `:440-460`. Then do the arithmetic on packed 16-bit
texels with channels split into two words, which halves the channel
decoding. The bilinear table entries hold it exactly, because the weights
are the same and only the order of operations changes; if rounding differs
by one anywhere, keep the current order and take only the packed decode.

Removes: three multiplies and six channel decodes per bilinear pixel.

### 7. Paired stores

Two adjacent visible pixels leave as one 32-bit store. On the aperture
each store is one bus transaction regardless of width, so this halves
write transactions on a VRAM target. A depth-rejected pixel must not be
written, so pairing applies only when both are visible; span ends fall
back to single stores.

This one waits for the parent plan's step 1, because it helps only the
VRAM target and the number that says how much is the one that measurement
produces. The table holds correctness; it cannot say anything about speed.

## The non-scalar finding this plan has to record

The engine accepts textures only in video memory (`d3d_soft.c:258`
refuses `DDSCAPS_SYSTEMMEMORY`; `:401` publishes `TEXTUREVIDEOMEMORY`),
and the depth buffer and blend destination live there too. Every textured
pixel is therefore one or four 16-bit reads through the PCI aperture,
every depth-tested pixel one more, every blended pixel another. Aperture
reads are not posted the way writes are; on a Pentium-era chipset each is
on the order of a microsecond. If that holds, a bilinear textured fill of
a 640x480 screen is seconds per frame, and nothing in the list above
changes it.

This is not asserted; it is the reason step 1 below adds a read
measurement to the parent's write measurement. If the read number is what
the reasoning suggests, letting textures and the Z buffer live in system
memory becomes the first change to make and this plan's items 2 to 7 fall
behind it. That change is an engine and caps change with its own record,
not a rasterizer one, and it is out of scope here beyond naming it.

## Work order

1. **Add a read to the parent's step 1 measurement.** The instrument that
   times an aperture write also times a 16-bit aperture read, on BARRY.
   One number, and it decides whether the sampler's memory placement
   precedes this list.
2. **Fix 1, incremental edges.** Land alone. Full table must pass
   unchanged. Record any entry whose expected pixels had to change, with
   the reason; there should be none.
3. **Fixes 2, 3, 4 together.** They touch the same loop body and are each
   bit-identical by construction. One commit, table unchanged.
4. **Fixes 5 and 6.** Textured path only. Table unchanged.
5. **Time it.** `dispbench`
   ([`dispbench-as-the-measurement-instrument.md`](dispbench-as-the-measurement-instrument.md))
   before and after on the Trio64 guest and, when available, on BARRY.
   Record per-scene frame times. The guest number measures the emulator's
   CPU model and says so.
6. **Fix 7**, only if step 1's write number says the aperture is the
   bound and the read number has not sent the work elsewhere.
7. **Decision record**, `docs/decisions/YYYY-MM-DD-rasterizer-scalar-fixes.md`,
   with the before and after times, what step 1 measured, and which of
   the rankings above the measurement disputed.

Steps 2 to 4 are two to three days of host-tested work. Steps 1 and 5 need
BARRY and are the ones that can slip.

## What this plan does not do

- No MMX, SSE or AVX. Those are the sibling plan's territory and depend on
  what remains after this.
- No change to caps, texture placement or surface residency. Named above
  as the likely dominant cost, and deliberately left to its own record.
- No change to the engine's vertex conversion in `d3d_soft.c`; the
  conversion is per vertex, not per pixel, and is not where the cycles are.
