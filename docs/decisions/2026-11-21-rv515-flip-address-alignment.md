# X1300 flip addresses keep only bits 31:12: back buffers placed on 4 KiB

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`.
Builds: measured on ati `44a4cf1b` (boot 47); fix in HAL `7f664531-dirty`
(boot 49, V9XHAL.DLL alone by WININIT rename; staging empty and the
hash matched after the reboot).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md).

## The report

The owner: GTA2 and Forsaken "works without rendering issues however the
screens for both games are horribly jittering". 3DMark 2000 looked right
to them.

## What was ruled out

The RS690/X1300 Direct3D path has no flip guard before a draw (the VI,
Rage 128, Intel and ViRGE paths do). But Forsaken's own clear already
waits for each flip: `BltFlipWaited=5971` against `FlipHandled=5991`
(boot 47 snapshot), so its draws come after the flip lands. Not the
cause there.

## What was measured

Forsaken at 800x600x16 flips between offsets `0` and `0xEA600` (the
snapshot's BltFlip records): the back buffer straight after the
primary, 512-byte aligned. `V9XRVMM` read `D1GRPH_PRIMARY_SURFACE_ADDRESS`
six times while it ran. Once it caught the second buffer:

| | value |
|---|---|
| written by the HAL | `D00EA600` |
| read back | `D00EA000` |
| D1GRPH_PITCH | `320h` (800) |

The register keeps bits 31:12. So every other frame was scanned from
1,536 bytes before the buffer: nearly a line early, 60 times a second.
`rs690_2d.h` had taken Evergreen's 256-byte rule for D1GRPH without an
AVIVO guide; on this card it is wrong. 640x480x16's back buffer sits at
`0x96000`, which is 4 KiB aligned, and that is why 3DMark (640x480)
looked right.

## The change

`v9x_d3d_place_flip_chain` (d3d_place.c): for a primary flip chain at
the mode's size, each back buffer gets its own block from DirectDraw's
heap, rounded up to 4 KiB, at the display pitch; the primary stays where
DirectDraw put it. Blocks are recorded by the address they serve and
freed by it on destroy, since the memory moves between the chain's
surfaces as it flips; the records are dropped at a mode change. Any
chain it declines (no primary address, wrong size, no room) is the
heap's, as before. The RS690 and X1300 Direct3D engine uses it with
`V9X_D3D_RS690_FLIP_ALIGN` (4 KiB); the RS690 has not been measured and
gets the same rule as the same AVIVO block.

## Boot 49: the placement never ran

The first build required the primary to have its address when the HAL
saw the chain. The owner played Forsaken and GTA2 at 800x600 and saw
the jitter unchanged ("like the screen is douplicated twice very
fast"); the trace showed the back buffer still at `0xEA600`, the heap's.
So that session tested nothing.

## Boot 50: placed

A `flip-create` / `flip-placed` pair now goes to `V9XPS.LOG`. It showed
the cause: the primary comes to CreateSurface with `fpVidMem` 0
(`flip-create 2 E238 0 03200258`: two surfaces, 800x600). The placement
now gives a primary without an address `vmiData.fpPrimary`, since a
handled call leaves nothing for the heap to fill. With that:

- `flip-placed 1 601C F5B3D000 03200258`: handled, the back buffer at a
  4 KiB-aligned address.
- Forsaken at 800x600x16, run by the agent for about 90 s: flips between
  `0` and `0xEB000`, `FlipHandled=2592` = `FlipTakenAtDone`,
  `M64Draws=1644229`, `M64Refused=0`. It closed normally and the desktop
  came back.

The owner then played Forsaken and GTA2 at 800x600: "nice, both games
are fixed!" (07:12Z). They had also tried vsync on and off on boot 49,
with no change, which fits a fault in the address rather than the
timing. `V9XPS.LOG` over their session shows four more chains, two at
800x600 (back buffer `F5B3D000`) and two at 640x480 (`F5AE8000`), every
one handled and 4 KiB aligned; a lone primary without a chain
(`dwSCnt` 1) was left to DirectDraw.

Not measured: the RS690, which takes the same placement.
