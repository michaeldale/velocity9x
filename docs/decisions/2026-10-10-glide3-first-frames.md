# GLIDE3X.DLL draws Rollcage and Diablo II on the Rage XL, and the chroma key needed the texture's alpha

Status: Measured. Both games draw through the real GLIDE3X.DLL on A8U4I5;
Glide 2 re-passed its gates on the netbook with the shared fix.

Date: 2026-10-10. Machines: A8U4I5 (ATI Rage XL PCI, Velocity9x 0.15.0
`ati`, boot 377); MICHAEL-NETBOOK (GMA 950, Gen3, boot 130). Plan:
[glide-3x-wrapper.md](../plans/glide-3x-wrapper.md). Evidence:
[docs/probe/a8u4i5-glide3-first-frames-2026-10-10/](../probe/a8u4i5-glide3-first-frames-2026-10-10/README.md).

## Context

The census DLL measured what Diablo II and Rollcage ask of Glide 3
([Diablo II](2026-10-10-diablo2-glide3-census.md),
[Rollcage](2026-10-10-rollcage-glide3-census.md)). The real DLL is a Glide 3
front end (`src/glide3/glide3_dll.c`) over the engine GLIDE2X.DLL draws
with (`src/glide/glide_core.c`). Vertices are read through the declared
layout into Glide 2 GrVertex floats, Glide 3's log2 LOD and aspect numbers
are turned into Glide 2's, and polygons, fans, strips and triangle lists
become triangles (`src/glide3/glide3_layout.c`, host-tested with the
census's own vertices).

## What happened

**Rollcage** drew on the first try: main menu, then Arcade into a race with
the city, the cars and correct depth (W-buffer from Q, one aux buffer).
1,437,354 triangles drawn, none refused, about 15 frames a second. Fog,
mipmaps and the LFB stayed unused, as the census found.

**Diablo II** stayed black. The log showed 33,650 triangles refused with
`UNSUPPORTED`, every one a keyed texture (P_8 converted to ARGB1555),
MODULATE colour and **fragment alpha**. Diablo II keys with an alpha
combine of ZERO or of the constant colour. The engine's chroma-key
approximation (`glide_state.c`) marks keyed texels with alpha 0 and adds
an alpha test, but left the alpha op as the combine asked: the fragment's.
Two consequences:

- The key never reached the alpha test, so on an engine that accepts the
  draw every keyed texel would have been drawn.
- The render interface's single-unit op table has no MODULATE colour with
  fragment alpha on a texture that has alpha, so the Rage XL refused it.

Glide applies the key before the alpha combine. The fix carries the
texture's key alpha to the test: alone (REPLACE) when nothing else reads
the source alpha, scaled by the fragment's (MODULATE) when a blend or the
game's own alpha test does.

**The first form of the fix broke NFS II SE.** It applied to every keyed
format. On the netbook its track map disappeared (A/B with the pre-fix
DLL, same key sequence, race clock 0:30 in both). The map is a format with
its own alpha (1555 or 4444), keyed under a vertex-alpha combine. Its own
alpha, which the game does not mean to use, was put in charge, and the
alpha test discarded the pane. The hypothesis that the key alpha could
stand in for every format is dead.

The final form promotes the alpha op only where the converted alpha is
the key alone: RGB_565 and P_8 (`v9x_glide_texfmt_key_alpha_only`). The
state mapping marks the promotion (`key_alpha`), and the texture bind,
which knows the format, keeps the fragment's alpha for the others. NFS II
SE then drew as before the fix (map, HUD, mirror, rain; 3,200,674
triangles, none refused), V9XGLIDP passed 21 of 21, and Diablo II drew
title, menus, character screen and town.

## What remains refused

On the final DLL, Diablo II drew 1,923,489 triangles from title to town
and the Rage XL refused 15,481 (0.8 %). They are keyed sprites blended
SRCALPHA/INVSRCALPHA under a translucent vertex alpha, so MODULATEALPHA
(At times Af). The Mach64 policy (`mach64_policy.c`, item 10) records that
the chip has no mode yielding AtAf and refuses it when the vertex alpha is
not opaque. That is the engine's limit, not the DLL's. Which sprites they
are was not identified on screen.

Rollcage on the final DLL drew 1,104,450 triangles and refused none. That
run's key reached the menu before it came up, so it shows a scene the game
chose (probably attract mode). Whether that scene is complete (a black
billboard, floating track pieces) is not known: there is no reference
frame.

## Gen3

The final GLIDE3X.DLL ran Rollcage on the netbook (GMA 950, boot 130) from
the extracted demo: the intro and an attract race on a Mars track drew,
4,171,158 triangles, none refused, 20 to 28 swaps a second. The same 48
`unrecognized` lines as on the Rage XL (an iterated colour beside a
texture alpha, drawn with the closest mapping). Diablo II is not
installed there.

## Options for the remainder

- Leave translucent keyed sprites refused on the Rage XL.
- Draw them without translucency (MODULATE, texture alpha only), which the
  engine takes, and accept opaque sprites where the game wanted see-through
  ones.

## Decision

Keep the key fix as above for both DLLs. Leave the Rage XL's refusals
until the sprites are identified and the trade-off can be judged on
screen.

## Consequences

- GLIDE2X.DLL changed with GLIDE3X.DLL: the state mapping and the texture
  bind are shared. NFS II SE and V9XGLIDP were re-run on the netbook with
  the final build.
- `V9X_GLIDE_DRAW_SETUP` has a `key_alpha` field, and `glide_texfmt.c`
  answers which formats key alpha alone.
- The Glide 3 plan's Phases 1 to 4 have first evidence on one engine; Gen3
  and packaging are still open.
