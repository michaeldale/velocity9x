# The DirectDraw probe: references, expectations, comparisons

`tools/diag/ddraw_probe_win32.c` builds to `V9XDDP.EXE`. Run on a guest it
writes `C:\V9XDIAG\V9XDD.INI`, one `key=value` per line: HRESULTs, raw 16-bit
pixel values read back from the render target, and `*Ok` flags. It is plain
DirectDraw and Direct3D, so it runs against any driver - ours or the vendor's -
on any chip, and that is how it is meant to be used.

## Three ways to read a result

1. **Absolute.** The `*Ok` keys. Each rung knows what a correct driver draws
   and says whether it saw it. Good for a quick look; blind to anything the
   rung's author did not predict.
2. **Regression.** The same binary, the same machine, the driver before and
   after a change: `scripts/compare-probe.ps1 -Left before.ini -Right after.ini`.
   Every difference is the change's.
3. **Reference.** Ours against the vendor's own driver on the same chip:
   `compare-probe.ps1 -Left vendor.ini -Right v9x.ini -Expect expectations/<chip>.txt`.
   The differences are the chip's work list, and the expectations file is
   where a known, accepted difference is recorded with its reason so it stops
   being noise without being forgotten.

## Layout

```
docs/probe/
  README.md               this file
  references/             result files worth keeping, named <chip>-<driver>-<date>.ini
  expectations/           <chip>.txt: one key per line, "# reason" after it
```

Reference files are evidence, so they are committed as captured, never
edited. A volatile key (heap pointers, handles, build ids, timings) is dropped
by the comparison, not from the file.

## What a first run on a new chip should look like

Run the probe under the vendor driver first and keep that file as the
reference. Run it under ours. Compare. For each difference decide: a defect
(fix it, the diff shrinks), a deliberate difference (add it to the chip's
expectations with the reason), or the vendor's own bug (also an expectation,
with the reason). When the unexpected-differences count is zero the chip is as
good as the vendor's driver by everything the probe measures - which is a
statement about the probe as much as the driver, so the next thing to add is
the rung that would have caught what the probe missed.

## The texture matrix

Every texture rung written before 2026-09-03 used one 64-texel texture with a
solid fill, and the three Trio3D defects found that day all lived outside that
point: a stride that only mattered above 64 texels, an alpha bit that only
mattered in one format, a level that only mattered on a gapped chain. The
matrix walks the space instead - sizes 64/128/256, ARGB1555/ARGB4444, plain
texture / DirectDraw-built two-level chain / hand-built chain with a filler
between the levels, filters NEAREST/LINEAR/MIPNEAREST/LINEARMIPLINEAR and one
alpha-blended cell - ninety cells, each drawn twice from a texture that is
green on the left and blue on the right (magenta and cyan on level 1), with the
target pixel classified by hue. Keys are `TexM_<size>_<fmt>_<layout>_<filter>_L`,
`_R`, `_Ok`; the summary is `TexMatrixOk` of `TexMatrixCount`; gapped chains
also record `_Delta`, the byte distance between the levels, so a reading is
trusted only when the gap is real.

A cell reading black on one half is a triangle that was not drawn or not yet
drawn when the target was read; that pattern, a different quarter of the cells
each run, is how the 3D-done wait in `eng_s3_virge.c` was found to be needed
(`docs/decisions/2026-09-03-the-probe-matrix-and-the-3d-done-bit.md`).

## Beside each cell: what the driver did

The probe reads a compact block of the driver's counters through the display
driver's DCI escape (`V9X_DDGETCOUNTS`, `include/velocity9x/probe_counts.h`)
before and after every matrix cell and every target, and writes a key only
when something moved: `_Dref` (textures refused), `_Dskip` (blends skipped),
`_Dfault` (engine timeouts and resets), `_Dmiss` (3D-done bit not seen). An
absent key means nothing happened. `TexMatrixCountsOk=0` means the driver did
not answer the escape - a vendor driver, or ours before it existed - and the
absence of deltas then means nothing. A cell whose draw returned a failure
writes `_Hr` as well.

The matrix also has a `wrap` cell (coordinates outside the first repeat; the
halves swap) and, for ARGB4444 only, a `halfa` cell (alpha 8 of 15, blended;
the blue channel must land between 70 and 190 of 255), 117 cells in all.

## Render targets of real sizes

`Tgt_<w>_*`: for 320x240, 640x480, 800x600 and 1024x768, an offscreen target
of that size with its own 16-bit depth surface, device, viewport and texture,
then three textured draws with depth on - ALWAYS at 0.5 (green), LESS at 0.75
(still green), LESS at 0.25 (blue). `_Pitch` is the pitch DirectDraw gave the
target. On the emulated ViRGE with a 1024x768 desktop, 320 and 640 pass all
three and 800 and 1024 are refused by DirectDraw at CreateSurface with
E_INVALIDARG (`_TargetHr`), before the driver is asked; a game at those sizes
renders to the primary chain in exclusive mode, which this block does not
cover. `TexMatrixMs` and `TargetsMs` time each block.

## State between rungs

`v9x_probe_reset_state` puts every render state back to the device's defaults
at the start of each rung group and each matrix cell. Before it existed the
first run after a reboot twice read rungs as undrawn that the next run read
correctly, because a texture handle or a blend left on by the previous rung
landed on whichever rung came next. A rung that needs a non-default state sets
it after the reset, visibly.

## The alpha transfer curve

`AlphaCurve_<a>_Raw`, `AlphaCurveB_*`, `AlphaCurveC_*` and
`VtxAlphaCurve_<a>_Raw`. A `*Ok` key says a rung's expectation was met; it does
not say what a failing part did instead. This rung says that. A known primary
fills the target, a uniform ARGB4444 texture is drawn over it nine times with
the texel's alpha stepped 0, 2, 4 ... 14, 15, and both ends are measured rather
than assumed - `*DstRaw` is the fill read back with nothing over it, `*SrcRaw`
the same texel drawn with the blend off. On a correct part the raws interpolate
linearly between the two.

The walk is taken three times over rotated operand pairs (blue over red, red
over green, green over blue), which is what separates a blend that mishandles
alpha from one an operand never reaches: if the output does not rotate with the
operands, they are not what the blender is using. It is then taken once more
with vertex alpha on an untextured triangle - the engine's other alpha path -
so a defect common to both is told apart from one that is not. That rotation is
what identified the Trio3D/2X defect:
`docs/decisions/2026-09-04-what-the-trio3d-blend-does-with-its-operands.md`.
