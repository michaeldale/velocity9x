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

## State between rungs

`v9x_probe_reset_state` puts every render state back to the device's defaults
at the start of each rung group and each matrix cell. Before it existed the
first run after a reboot twice read rungs as undrawn that the next run read
correctly, because a texture handle or a blend left on by the previous rung
landed on whichever rung came next. A rung that needs a non-default state sets
it after the reset, visibly.
