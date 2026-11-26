# Riva 128: the owner's game notes on 0.59.0

Date: 2026-11-21

Machine: M-7V636C (Mac's RIVA 128ZX, `12D2:0018`, Pentium II 333, Intel
AL440LX, 256 MB, Windows 98 first edition). Status: open, nothing
measured.

Source: the owner's text file `C:\WINDOWS\Desktop\V9x Riva 128 Driver
Notes.txt` (1491 bytes, dated 2026-11-20 inside), read over the relay
with `v9xctl cat` on 2026-11-21. The owner announced it in chat at
2026-11-21 06:02Z.

Not checked: the installed build. The notes say 0.59.0; `C:\WINDOWS\
V9XHW.INI` was not at that path, and no other file was read. Desktop
mode, colour depth and each game's settings during the owner's runs are
not known. Every line below is the owner's observation by eye, not a
capture or a counter.

## What the owner reported

Desktop

- Dragging windows: the refresh is very low and the window is often
  drawn over its old position briefly, leaving a trail.

Direct3D

- Final Fantasy VII: very low framerate in game and in full-screen
  videos. 2D backgrounds black, except for some layers, occasionally.
- Thief: The Dark Project: lightmaps wrong. Many seams between
  polygons, which the owner says NVIDIA's own driver shows too.
- Thief II: The Metal Age: lightmaps wrong. The skybox draws on top of
  the terrain.
- Half-Life: very low framerate. Renders correctly apart from seams
  between polygons.
- Unreal Tournament (Direct3D): very low framerate, lightmaps not
  working properly, menus look oddly low resolution.
- Jedi Knight: Dark Forces II: the mouse disappears in menus. Movies
  render correctly at full speed; in game renders correctly.
- Total Annihilation: Kingdoms: `ChooseRenderer.exe` offers Direct3D
  only on the Voodoo. NVIDIA's own driver was not offered either; the
  owner believes this is because the game is DirectX 6.

OpenGL

- GLQuake: very low framerate, lightmaps not working.
- Quake II: very low framerate. Renders correctly apart from seams
  between polygons.
- Homeworld: menus in OpenGL mode are very slow and slightly corrupted.
  The owner could not navigate far enough to test in game.
- Unreal Tournament (OpenGL): displays grey only.

## What the records already say

- Thief ran here on boot 36 (2026-11-04, f6ab7dd3) with 719k draws and
  none refused
  ([decision](../decisions/2026-11-04-riva128-thief.md)). A draw count
  does not judge lightmaps; that run does not dispute the owner.
- Half-Life c1a0 ran windowed on boots 27 and 44 with no refusals at
  the end ([decision](../decisions/2026-11-04-riva128-half-life-d3d.md)).
  Framerate was not measured.
- Unreal Tournament Direct3D: ONE/INVSRCCOLOR is drawn as an add, an
  approximation chosen on 2026-11-04, and ONE/SRCALPHA was refused
  ([decision](../decisions/2026-11-04-riva128-unreal-tournament.md)).
  Either could be the lightmap fault there. Not tested.
- 16 bpp GDI acceleration is off on this chip
  ([decision](../decisions/2026-11-07-riva128-16bpp-2d-bit15-visible.md)),
  so a 16 bpp desktop draws window moves on the CPU. That fits the slow
  drag; it does not explain a trail. The owner's desktop depth is not
  known.
- Final Fantasy VII, GLQuake, Homeworld, Jedi Knight and TA: Kingdoms
  have no earlier run on this card.

## Hypotheses, none tested

- The lightmap faults in Thief, Thief II, GLQuake and Unreal Tournament
  share a cause: a blend the NV3 engine has no exact form for (the
  modulate blends DESTCOLOR/ZERO and DESTCOLOR/SRCCOLOR are the usual
  lightmap passes), drawn as something else or refused.
- The seams are the chip's, since the owner sees them on NVIDIA's
  driver in Thief. The HAL floors the vertex fraction
  ([decision](../decisions/2026-11-03-riva128-d3d-state.md)).
- The Thief II skybox is a depth state the HAL maps wrongly (a draw
  with Z writes or the compare off).
- The Unreal Tournament OpenGL grey screen and the Homeworld menus are
  the ICD on the NV3 render interface, which still refuses a write mask
  other than RGB and CPU texture levels
  ([issue](2026-11-07-riva128-render-interface-has-no-engine.md)).

## Next

Per title, a windowed run with V9XTRACE snapshots before and after,
reading the refusal counters and the blend pairs the game sent. Thief
(boot 36 recipe) and GLQuake first, since each shows the lightmap fault
alone. Read the installed build's identity before the first run.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues. The RIVA
128ZX is Experimental for 1.0 (decision 3); the README's RIVA 128ZX
entry links this record.
