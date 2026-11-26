# Rage IIC: 3DMark 2000 stops in a loop of depth fills, and V9XD32's off-screen render targets stay empty

Date: 2026-11-26. Machine: A8U4I5, Rage IIC AGP (`1002:4757`, 4 MiB),
the 0.61.0 tree's `ati` set, boot 704. Status: open, cause not looked
at. First V9XD32 and first 3DMark 2000 run on this card.

## 3DMark 2000 stops

The default benchmark at 640x480x16 (16-bit textures and Z, triple
buffer; the default 1024x768x32 needs 16 MB). After 1,340 flips it
stopped presenting: three snapshots over 80 s show `FlipHandled`,
`M64Draws` (328,490) and every draw counter still, while `CountBlt`
went 20,335 to 48,375 and the trace ring is nothing but `Blt` entries
with flags `02000000` (`DDBLT_DEPTHFILL`) and exit 1 (handled). 3DMark
was down to one thread; the screen was black but for one white streak
of rotor
([screen](../probe/rageiic-1.0-2026-11-26/3dmark-640-stall/screen-while-stuck.png)).
Windows and the agent stayed responsive, and killing 3DMark returned
the desktop at 800x600. Before the stop the engine refused 7,069 draws
(`M64Refused` 7 to 7,076, last reason 7, texture format).

So 3DMark re-issues a depth fill without end. Whether our depth fill
reports success while leaving something 3DMark checks (the Z surface's
contents, a lost surface) is not known.

## V9XD32 T4 and T16

Both draw into an off-screen render target of their own. T4's clear
returns `E_NOTIMPL` (`T4ClearHr=80004001`) and 990 of 3,861 judged
pixels are wrong; T16 (the DirectX 7 device on its own target) reads 0
for its clear, textured and untransformed pixels with every call
succeeding. On the Rage IIC, off-screen render targets do not receive
the clear or the draws.

Evidence: [`../probe/rageiic-1.0-2026-11-26/`](../probe/rageiic-1.0-2026-11-26/)
(`rageiic/v9xd32-bpp16-V9XD32.TXT`, `3dmark-640-stall/`). Record:
[decision](../decisions/2026-11-26-1.0-rageiic-a8u4i5.md).

## 2026-11-26, later: Half-Life and 3DMark 99, which ran in October

Michael asked for a program that worked on this card in an older
release.

- **Half-Life Direct3D** (`hl.exe -d3d -w 640 -h 480 -full -console
  -condebug +timedemo mwd5`, the October command, which ran at
  4.29 fps on our driver,
  [record](../decisions/2026-10-03-rage-iic-half-life-d3d-against-ati.md)): the
  machine locked during the demo; the console log ends at "Playing
  demo"; Michael power-cycled it (boot 704 to 705). No fault record was
  written. **A regression from October.**
- **3DMark 99 Max**, its default 800x600x16, Z16, triple buffer: 24 of
  26 tests ran with nothing refused and no engine timeout (Trilinear
  Filtering drawn correctly), then at Image Quality 1 it stopped in
  the same loop as 3DMark 2000: `FlipHandled` and `M64Draws` still at
  1,730 and 146,731 for 24 s while the trace ring filled with
  `DDBLT_DEPTHFILL` Blts. Killed. In October 3DMark 99 was judged
  "looking good" by eye but its score was never read, so whether Image
  Quality finished then is not known.

Evidence: `../probe/rageiic-1.0-2026-11-26/half-life-d3d/` and
`3dmark99/`.

## 2026-11-26, night: what the depth-fill loop is, and the Half-Life lock

Temporary diagnostic builds (not committed; the patch is kept outside
the repository) traced each depth fill and the Mach64 core. 3DMark 99
Max with only its Image Quality tests reproduces the stall in about a
minute, after 23 draws.

**The loop.** Every depth fill in the stall takes one path: the engine
fill is declined because the shared Mach64 core is quarantined, the
render drain then fails at once (`wait_idle` returns
`INVALID_ARGUMENT` on a quarantined core, uncounted), and
`v9x_depthfill_body` answers `DDERR_WASSTILLDRAWING`, which the runtime
retries forever. `FIFO_STAT` reads `0000FFFF`: all sixteen entries
occupied, the engine wedged.

**Why the core is quarantined.** Not a FIFO error: a history of the
last 64 register writes, frozen at the first FIFO error, never froze.
It froze instead at a wait-for-idle timeout with the FIFO empty and the
engine still busy (`v9x_m64_wait` then ran `v9x_m64_reset_replay`,
which failed and quarantined the core). The writes before it: one
Rage II trapezoid (DST_Y_X x 16, y 159, three pixels wide, one row,
`DST_BRES_LNTH` `80128001`), the 2D mode switch, and a 256x256 depth
fill at (0,0) with `FFFF` (`DST_HEIGHT_WIDTH` `01000100`). Nothing in
those writes is out of range; what wedges the engine is not found.

**Not a regression.** Released 0.10.0 (3 October, which holds the build
Half-Life ran on) stops at Image Quality 1 the same way.

**The Half-Life lock goes with the agent's VNC server.**

| Build | `V9XVNC.EXE` | Half-Life Direct3D mwd5 |
|---|---|---|
| 0.61.0 tree | running | machine locked |
| 0.10.0 | running | machine locked |
| 0.10.0 | stopped | 394 frames, 5.233 fps |
| 0.61.0 tree | stopped | 394 frames, 5.199 fps |

October's runs (5.57 fps) had an agent without VNC. So the "regression"
was the test machine: the VNC server reading the screen while the Rage
II draws. With VNC stopped, 3DMark 99 Image Quality on the 0.61.0 tree
still wedged the engine, and this time the machine locked instead of
looping. Each lock needed a power cycle (boots 704, 710, 712, 714).

Still open: what wedges the engine in Image Quality 1 (and 3DMark 2000);
the depth fill answering `WASSTILLDRAWING` forever on a quarantined
core, where it should fail or fall back; whether a screen read during
Rage II drawing should be made safe in the driver.
