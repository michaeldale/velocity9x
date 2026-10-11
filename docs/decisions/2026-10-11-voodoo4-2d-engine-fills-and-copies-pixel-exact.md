# The Voodoo4's 2D engine fills and copies pixel-exact from the builder's values

Date: 2026-10-11. Machine: A8U4I5, boot 410, Velocity9x `3dfx` tier-0 at
640x480x16. Card: retail Voodoo4 4500 AGP, `121A:0009` subsystem `0004121A`.
Evidence: [a8u4i5-voodoo4-2d-2026-10-11](../probe/a8u4i5-voodoo4-2d-2026-10-11/).
Tool: `TDFX2D.EXE` + `TDFX2D.VXD` (`scripts\build-tdfx-2d.ps1`), which
programs the engine with exactly what `src\chipsets\tdfx\tdfx_2d.c` builds.
Plan: [3dfx-vsa100-family.md](../plans/3dfx-vsa100-family.md), Phase 3
step 1. Register meanings:
[vsa100-registers.md](../specifications/vsa100-registers.md) section 6.

## Method

A 64x32, 16-bpp surface at VRAM 16 MiB, pitch 128 bytes: off the desktop,
inside the BIOS's linear view, in heap nothing had allocated. Per test: prefill
(constant `A5A5` or a pixel-index gradient) through BAR1, wait idle (status
bit 9 clear on three consecutive reads), the builder's register writes
through BAR0 `100000h`, wait idle, read the whole surface back, compare with
a C model. The first engine write on this card was the first test.

## Measured

| Test | Command | Reads to idle after | Result |
|---|---|---|---|
| Fill 24x20, ROP0 `CC` | `CC000105` | 7 | pixel-exact |
| Fill 24x20, ROP0 `F0` | `F0000105` | 7 | 260 of 1024 dwords differ |
| Copy 24x20, disjoint | `CC000101` | 8 | pixel-exact |
| Copy 24x20, 4 px right, overlapping | `CC004101` | 8 | pixel-exact |
| Copy 24x20, 4 rows down, overlapping | `CC008101` | 10 | pixel-exact |
| Fill 8x8 after a NOP (`/small`) | `CC000105` | 5 | pixel-exact |

Status read `5F` before and after both runs; every wait before an operation
took five reads. Run 1 without `/small`, run 2 with it.

## What this shows

1. **The builder's encodings are right on silicon** for fill and for copy in
   all three directions it chooses, including the starting-corner rule for
   right-to-left and bottom-to-top.
2. **A rectangle fill paints `colorFore` with ROP0 `CC`.** `F0` paints
   something else (the pattern registers, not set here). The builder uses
   `CC`.
3. **The engine needs no enable step** under the BIOS's VBE mode: no
   init register was written by the probe or the driver.
4. **An 8x8 fill preceded by a NOP completes.** This says nothing about the
   research notes' claim that a small operation *without* the NOP hangs;
   that case was not run.

## Not established

- 8 and 32 bpp, other pitches, surfaces above 16 MiB.
- The small-blit hang without the NOP.
- Throughput, and behaviour under load or with the desktop being drawn.
- How the engine behaves on a mode set while busy.
