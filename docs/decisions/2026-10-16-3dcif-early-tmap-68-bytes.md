# 3DCIF: the earliest C3D_TMAP is 68 bytes, and Assault Rigs registers it

Date: 2026-10-16

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI, Velocity9x `ati`, boot
482, 5:5:5 desktop). Evidence:
`docs/probe/a8u4i5-rage-xl-assault-rigs-2026-10-16/`.

## What happened

Assault Rigs for 3D Rage 1.0.0.3 (the ATI Xpression OEM disc, from
archive.org `assault-rigs-3-d-rage`; installed by hand to `C:\ARIGS`, see
[the 3DCIF plan](../plans/ati-3dcif-wrapper.md)) drew its menus and levels
as plain white geometry. Nothing was refused or skipped, and `TextureReg`
was called 8,678 times in a few seconds with no `TextureUnreg`.

A note added to the refusal path showed why: every call was
`TextureReg refused BADPARAM: size=68 format=3 lg2=0606` (RGB1555, 64x64
and 64x32). The DLL required at least 72 bytes, the `C3D_TMAP` that ends
at `htxpalTexPalette`. Assault Rigs' ends at `clrTexChromaKey`, 68 bytes,
with no palette handle. It treated the refusal by registering again on
the next draw, and drew with a handle that was never written.

glrage reads `u32Size` only to decide whether the clamp flags exist
(`> 68`), so it never met this.

## The change

`cif_dll.c` accepts a `C3D_TMAP` of 68 bytes or more and reads the
palette handle only when the structure has one (72 bytes or more). A
refused registration and the first 16 accepted ones are now logged with
their size, format and dimensions.

## Result

Same machine, same boot, the rebuilt DLL in `C:\ARIGS`:
- Every registration accepted (`tmap size=68 format=3`); 62
  registrations for the session instead of thousands.
- The menu, level select and a level draw textured
  (`menu-after.png`, `level-select-after.png`, `level-after.png`; the
  agent's screenshots, which miss some flipped frames).
- 1,052,295 triangles drawn, none refused, over 3,021 frames.

## Also seen

- The game's menus run in a 400x300 16 bpp mode, which has no 15 bpp
  sibling and stays 5:6:5: the render interface describes a 565 target
  and the DLL logs "the display is 5:6:5". The menus looked right in the
  agent's capture. Its 640x480 loading screen ran at 5:5:5.
- Not tested: control in play, sound (A8U4I5 has no sound device; the
  game imports DirectSound and runs regardless), the CD audio.

Gates: run-checks. No host test: `TextureReg` reads the application's
memory through the DLL's own entry point, which the host tests do not
build.

## Wipeout: the same structure, and report V9X-AQTFNK

Wipeout for 3D Rage (archive.org `All-In-Wonder`, `WIPEOUT_ATI_3DRAGE.BIN`;
installed from the mounted disc to `C:\3D Rage Games\Wipeout`, which
holds only a save file; the game runs from D:) registers its textures
with the same 68-byte `C3D_TMAP` (`tmap size=68 format=3 lg2=0808`).
Evidence: `docs/probe/a8u4i5-rage-xl-wipeout-2026-10-16/`.

- Report V9X-AQTFNK (0.18.4, a GMA 950) says Wipeout showed a black
  screen and error boxes that could not be seen, cleared with Enter
  twice. Its snapshot shows our DLL described the device and nothing was
  drawn. Wipeout's executable carries "Error registering texture for
  file %s": with every registration refused, that is the likeliest
  error. Not confirmed on the reporter's machine.
- With the DLL from `61c36edf` the intro plays and the title screen
  draws: three 256x256 textures registered, 11,660 triangles drawn and
  none refused.
- With ATI's own ATI3DCIF.DLL, which A8U4I5 still has in SYSTEM, it
  stops at "5000: Could not initialise CIF". That is ATI's DLL
  needing ATI's driver, not this defect.
- Its title runs at 512x384, which has no 15 bpp sibling and is
  5:6:5; the backdrop, which the game writes as 5:5:5, shows as green
  and purple noise
  ([issue](../issues/2026-10-16-ati-16bpp-modes-without-a-555-sibling.md)).
