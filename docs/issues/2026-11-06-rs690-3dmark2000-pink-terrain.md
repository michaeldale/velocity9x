# M-GB8KRE (RS690): 3DMark 2000's first game test draws some far terrain triangles salmon-red

Date: 2026-11-06. Machine: M-GB8KRE (ATI Radeon X1200 IGP, 1002:791F,
Win98 SE, DirectX 8.0), boot 84. Build: main's tree at e5f3a34f, the ati
package's eight files, clean. Michael: record it, not a blocker for
0.37.0.

## What was seen

The default benchmark ran at 1024x768x16 with V9XTRACE `-grab:40x10`
and scored 1610 (1781 at boot 80 without grabs). The ten grabs, all
from Game 1 (Helicopter), draw the terrain, trees, helicopter, rotor
and HUD. But in most of them, whole triangles of the distant hillsides
are tinted salmon-red, with hard straight edges
(`docs/probe/rs690-0.37.0-checks-2026-11-06/3dmark-grabs-sheet.jpg`,
`3dmark-grab2-top.png`). Sampled colours sit around D87060h-E8A090h.
The Rage 128's grabs of the same scene in the 0.36.0 release checks
fade those hills into pale grey haze, with no red
(`docs/probe/0.36.0-release-checks-2026-11-06/rage128/3dmark2000-grab/grabs-sheet.jpg`).

## What it is not, as far as read

- **Not a refusal.** The triangles are drawn. The run's snapshot
  (`3dmark-V9XSNA7.INI`) counts 1,558,423 draws and 176 refused, which
  is 6 colour key, 6 texture format and 164 texture filter. Some of
  those may be the probes that ran earlier in the boot. A refused draw
  is not drawn at all, so refusals do not explain a tinted one.
- **Probably not the fog colour.** The edges are hard, so this is per
  triangle, not a per-pixel fog blend. The fog constant's float24
  conversion and channel order were read and look right, and V9XD32's
  T20 fog (blue, full and half) passes.

## Not known

- Which draw state those triangles carry: texture format, stage
  operation, blend or specular. A V9XTRACE state capture of Game 1's
  terrain draws would say.
- Whether the boot 80 run (no grabs) showed it. No frame was captured
  then.

## Next

Capture the terrain draws' states and compare with what
`d3d_rs690_map.c` maps. The likeliest suspects are a texture format or
stage operation only Game 1 uses.
