# Netbook (945GSE): Carmageddon II's HUD panels under delta0, 2026-10-15

[GitHub issue #5](https://github.com/michaeldale/velocity9x/issues/5),
comment of 2026-10-13: on the reporter's GMA 900 (0.17.0, Steam build,
Glide) the in-race HUD panels draw wrong. The Steam store's own screenshots
of the game show them as dark translucent blue-grey bars; the reporter's
show red rectangles behind the top bar's text and a red bottom bar.

MICHAEL-NETBOOK, boot 182, the retail game, `CARMA2_HW.ICD`, its game-folder
`GLIDE2X.DLL`. Michael drove the race; the agent's input does not reach the
game.

## Before (`GLIDE2X.DLL` 9a4c4473-dirty)

- `before-red-panels.png`: a HAL frame capture (80x60, enlarged) in the
  race. The same red panels as the reporter's.
- `glide-log-hud-panel-before.txt`: the panels are untextured triangles at
  `oow=0`, drawn with `guColorCombineFunction(3)`
  (`GR_COLORCOMBINE_ITRGB_DELTA0`), `grTexCombineFunction(0)`, blend 1,5 and
  `grConstantColorValue4` at `B0000000`. Their vertex colours are never
  written: `03ABDBA8`, `00000008`, `0081FAB0`.
- `grSstWinOpen` `cformat=0` (ARGB), so colour order is not involved.

## The cause

Under delta0 a Voodoo iterates `grConstantColorValue4`'s colour, flat, and
does not read the vertex colour. That register is separate from
`grConstantColorValue`'s, which the CONSTANT inputs read. Source: Dege
(dgVoodoo) on VOGONS, https://www.vogons.org/viewtopic.php?p=121229, who
replaces every vertex colour with it under delta0. No 3dfx binary or source
was read.

The DLL mapped ITRGB_DELTA0 to the vertex colour, so the panels took the
unwritten values. It also stored `grConstantColorValue4` in
`grConstantColorValue`'s register.

## After (`GLIDE2X.DLL` fd8bed8c-dirty, this change)

- `after-delta0.png`: the top and bottom bars are dark translucent
  blue-grey, as in the Steam screenshots. Michael, at the screen: "it looks
  a lot better".
- `glide-log-after.txt`: presets 2, 3, 4 and 6 only. Preset 7
  (TEXTURE_TIMES_ITRGB_DELTA0) is never set, so the textured world is not
  affected. 2,196,466 draws, 0 refused. The four stubbed calls are the same
  four as before.

Not checked:
- the menus (the frame captures are of the race);
- the bottom bar's diagonal stair-step lines in the reporter's screenshot,
  which an 80x60 capture cannot resolve;
- the reporter's machine.
