# Glide 2 for DOS games: a glide2x.ovl

Date: 2026-10-19. Status: parked. Michael parked it on 2026-10-19 until the
DOS-box round trip works
([dos-box-vdd-virtualization.md](dos-box-vdd-virtualization.md)).

Request: [GitHub issue 8](https://github.com/michaeldale/velocity9x/issues/8).
DOS titles with 3dfx support (Tomb Raider 1, GTA1, Red Baron named there)
load Glide from `GLIDE2X.OVL`, not `GLIDE2X.DLL`.

Nothing is coded or measured. Everything below is desk assessment of the
tree as of `6e982c6c`.

## What carries over from the Windows wrapper

The Windows `GLIDE2X.DLL` ([glide-2x-wrapper.md](glide-2x-wrapper.md)) is
a Win32 DLL that draws only through DirectDraw surfaces and the render
interface `V9XHAL.DLL` exports. A DOS program has neither.

- **Reusable as-is**: the pure, host-tested logic, about 1.6k lines:
  `src/glide/glide_state.c`, `glide_vertex.c`, `glide_texmem.c`,
  `glide_texfmt.c`.
- **Rewrite**: `glide_surface.c` (window, `SetDisplayMode`, flip chain,
  `Lock`, the `V9XHAL.DLL` load) and the Win32 parts of `glide_core.c`
  (`VirtualAlloc`, `Sleep`/`GetTickCount` pacing, the `CreateFileA` log).
- **No path to the engines**: every hardware engine (Gen3, Haswell, MGA,
  SiS, Mach64, Rage II, ViRGE) runs inside the HAL under DirectDraw. The
  software rasterizer `src/display32/d3d/d3d_raster.c` is pure C with a
  host test and is the only engine portable without DirectDraw.
- **No build target**: every DOS tool in the tree is 16-bit small model
  (`-bt=dos -ms`). There is no 32-bit flat DOS (DOS/4GW, DPMI) build and no
  DOS-side LFB mapping.

## Not established

- The OVL's container format, how the game's extender loads it, and its
  export list. Needs a real `GLIDE2X.OVL` taken apart and recorded in
  `docs/decisions/` before any code.
- Whether each named title loads the OVL or links Glide statically.
- Whether 3dfx's own DOS OVL runs on the Voodoo4. On Voodoo cards 3dfx's
  OVL already serves these games; Velocity9x adds value only on non-3dfx
  chips.

## Options, cheapest first

1. **Pure DOS, software engine.** A flat 32-bit OVL: VBE 2.0 mode set, LFB
   mapped through DPMI, `d3d_raster.c` drawing. Works on any card with a
   usable VBE LFB, which
   [dos-vbe-conformance.md](../specifications/dos-vbe-conformance.md) shows
   is not every card. Slow on period CPUs. Comparable in size to the
   Windows wrapper's Phases 1-3.
2. **DOS box bridged to Windows.** The OVL forwards calls through the
   mini-VDD to a Win32 helper running the existing engine, so hardware
   engines come free. A new VxD call path (a layering change needing
   agreement), and it depends on full-screen DOS surviving a mode switch,
   which is the open DOS-box work.
3. **Hardware engines in pure DOS.** Each family's 3D engine re-hosted
   outside the HAL; one port per family.

## When picked up

1. Confirm the DOS-box round trip is fixed on at least one machine.
2. Take apart a real `GLIDE2X.OVL` (format, loader, exports) and record it.
3. Census one title (Tomb Raider 1 is the usual reference), as Phase 0 of
   the Windows wrapper did for NFS II SE.
4. Choose between options 1 and 2 on that evidence.
