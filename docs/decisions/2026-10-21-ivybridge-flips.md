# Ivy Bridge flips on W9QI4E14: plane A's base and PRI_LINOFF, measured with a sub-page back buffer

Date: 2026-10-21
Status: **measured.** These are DirectDraw page flips on the HD 4000, step 5
of `docs\plans\intel-ivybridge-gen7.md`. Two `V9XDDP` runs (640x480x16
and 800x600x16), with the plane read back during the second.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-flips-2026-10-21\`. Package
`build\win98se-intel-gma`, build `c816c638-dirty` (DRV and HAL agree:
`BuildsAgree=1`).

## What changed

- **`V9X_DD_ENGINE_CAP_GEN7_IVB`** (`00080000h`, `engine_abi.h`). This is
  a chip-identity bit like the GM965's `I9XX_GEN4`. The Ivy Bridge
  descriptor stamps it with `FLIP`. EngineCaps is `00080007`, and the
  manifest adds `FLIP`.
- **`v9x_intel_ivb_plane_split`** (`hsw_display.c`, host-tested). It
  returns the 4 KiB page for `DSPASURF` and the rest, in bytes, for
  `PRI_LINOFF` (`V9X_INTEL_IVB_DSPALINOFF`, `70184h`). A rest that is not
  a whole pixel is refused (IVB PRM Vol 3 Pt 3 5.3: "at least pixel
  aligned").
  - The tests are worked by hand. 3DMark 99's 800x600x16 back buffer
    `1D4C00h` gives LINOFF `C00h`, where Haswell writes x 1536. A
    straight-after back buffer `EA600h` gives `600h`.
  - Changed to return pixels (Haswell's encoding), the split fails two of
    the new tests.
- **`hsw_scanout.c`** picks the offset register by the identity bit:
  `PRI_LINOFF` then `DSPASURF` on Ivy Bridge, and `DSPOFFSET` then
  `DSPASURF` on Haswell, unchanged. The displayed offset is `SURFLIVE` +
  `LINOFF` on Ivy Bridge.
  - Everything else is the Haswell scanout: live only with pipe A and
    plane A enabled; pending until `DSPASURFLIVE` equals the base written;
    the line from `PIPEADSL` + 1 against `6000Ch`. Both are the same
    registers on Ivy Bridge (both PRMs; the +1 is i915's
    `intel_vblank.c` for non-HDMI outputs).
- **`V9XDDP /800x600`**: the probe's flip chain at 800x600x16 instead of
  640x480x16. Every 640x480 depth puts the back buffer on a page boundary
  (`4B000h`, `96000h`, `12C000h`), so LINOFF would always be 0 there.

## Boot 7: V9XDDP at 640x480x16

Have Disk over boot 6's install (Windows warned the installed driver was
a closer match; answered Yes), then `reset` (boot 6 to 7). V9XDDP default
(`boot7-ddp-V9XDD.INI`):

- `SetModeHr` 0, a 640x480x16 flip chain, `FlipHr` 0;
- `Flip20Ms` 327, `FlipMaxMs` 17: twenty `DDFLIP_WAIT` flips paced at
  the display's 60 Hz;
- `FlipPixelOk=0`. That is the GDI readback this probe has always failed
  under real flips: the 4570 reads `Flip20Ms=329`, `FlipPixelOk=0`
  (`hsw-4570-32bpp-textures-flip-wait-2026-10-20`).

The snapshot (`boot7-ddp-V9XSNA3.INI`, `V9XTRACE` after the run;
`V9XSNAP.INI` beside it there is from before the install):

- `FlipHandled` 23, `FlipTakenAtDone` 23, `FlipNotTakenAtDone` 0;
- `FlipDeclined` 0, `FlipForcedIdle` 0, `DrawsIntoPresented` 0.

Every flip the HAL called done had `DSPASURFLIVE` holding the base it
wrote.

## Boot 7: V9XDDP /800x600 /hold, with the plane read during it

V9XDDP was started detached. `V9XIVMM.EXE` (read-only, no forcewake) was
run 14 times through the relay while it held
(`samples-800x600\sample*.INI`).

| Sample | t | PIPE_SRCSZ | PRI_STRIDE | PRI_SURF | PRI_LINOFF | PRI_SURFLIVE | PRI_TILEOFF |
|---|---|---|---|---|---|---|---|
| 1 | 16 s | `031F0257` 800x600 | `640h` 1600 | `000EA000` | `00000600` | `000EA000` | 0 |
| 2 | 28 s | the same | | | | | |
| 3-14 | 42-185 s | `027F01DF` 640x480 | `280h` 640 | 0 | 0 | 0 | 0 |

**The plane was scanning the back buffer through PRI_LINOFF:** base
`EA000h`, linear offset `600h`, start `EA600h` = 960000 bytes, which is
exactly 800 x 600 x 2. `DSPASURFLIVE` had latched the base, and
`PRI_TILEOFF` was never written. After the run the mode was restored to
the 640x480x8 desktop at base 0 with LINOFF 0.

V9XDDP itself (`boot7-ddp800-V9XDD.INI`): `SetModeHr` 0, primary
800x600x16 pitch 1600, `Flip20Ms` 330, `FlipMaxMs` 17, fill and copy pixel
checks 1, `Hold=active`, `RestoreHr` 0, `Result=COMPLETE`.

Snapshot (`boot7-ddp800-V9XSNA4.INI`), cumulative over both runs:

- `FlipHandled` 46, `FlipTakenAtDone` 46, `FlipNotTakenAtDone` 0;
- `FlipDeclined` 0, `FlipForcedIdle` 0, `DrawsIntoPresented` 0;
- EngineCaps `00080007`, no engine timeouts or resets.

## Not measured

- What the monitor showed. Nobody watched the AOC, and the evidence is
  the register state and the HAL's counters.
- A game's flip chain, triple buffering, and 8 and 32 bpp chains. The
  arithmetic covers 1, 2 and 4 bytes per pixel, but only 2 has run.
- Ring flips (`FLIP_RING`). These are register flips only, as on Haswell.
