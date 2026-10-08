# Glide 2.x on Velocity9x: a glide2x.dll over the render interface

Date: 2026-10-08

Status: in progress. Phase 0 done: census DLL built, NFS II SE's calls
measured ([2026-10-08-nfs2se-glide-census.md](../decisions/2026-10-08-nfs2se-glide-census.md)).
Phase 1 done: `glide_vertex.c`, `glide_texmem.c`, `glide_texfmt.c` and
`glide_state.c` with host tests; chroma key approximated in the DLL
(keyed texels to alpha 0 plus the alpha test), no ABI change.
Phase 2 done: the flip chain is a render target on Gen3 and the software
engine ([2026-10-08-glide-phase2-flip-chain.md](../decisions/2026-10-08-glide-phase2-flip-chain.md));
no Blt fallback needed. Phase 3 done: textures, palettes, chroma key,
blending, table fog and batching, 21/21 on both engines, and both fog
through the render interface
([2026-10-08-glide-phase3-textures-fog.md](../decisions/2026-10-08-glide-phase3-textures-fog.md)).
Phase 4 in progress: NFS II SE races on the netbook with everything
drawn ([2026-10-09-nfs2se-races-on-glide-gen3.md](../decisions/2026-10-09-nfs2se-races-on-glide-gen3.md)),
at about 18.5 fps with three buffers and no flicker in game
([2026-10-09-glide-frame-rate-netbook.md](../decisions/2026-10-09-glide-frame-rate-netbook.md));
the pause and exit dialogs draw wrongly.
First game: Need for Speed II SE. First engine: Gen3 on the netbook.

## Context

Many 1996–98 games accelerate only through 3dfx Glide. Need for Speed II SE
is the first target. Its accelerated path needs `glide2x.dll`, and no chip
Velocity9x drives is a 3dfx part. The OpenGL ICD has already shown the route:
a user-mode DLL that never touches hardware. It draws through
`V9xRenderInterface` (`include/velocity9x/r3d_abi.h`, ABI 4), exported by
`V9XHAL.DLL`, into DirectDraw surfaces it owns. A Glide 2 DLL built the same
way becomes a second client of that interface, and every engine that serves
the ICD serves Glide too.

Chosen with Michael:
- **First target:** the netbook (GMA 950, Gen3). It is the only engine whose
  refused draws fall back to the software engine.
- **No Voodoo reference run.** NFS II SE's actual Glide usage is established
  on the netbook with a logging stub (Phase 0), not by assumption.

Not in scope: Glide 3 (`glide3x.dll`), the 3dfx family's own Glide
coexistence question (`docs/plans/3dfx-voodoo3-prior-work.md`), and
installing system-wide on 3dfx families.

## Licence rule (from 3dfx-voodoo3-prior-work.md)

Open Glide (`sezero/glide`) and the leaked 3dfx tree may be read for facts:
API semantics, struct layouts, constant values. Nothing is copied. The public
Glide 2.4 Programming Guide and Reference Manual is the authority each
API-level constant cites, the way register code cites a databook. Record
which document and revision in the Phase 0 decision record.

## Design

### Module layout: `src/glide/`, mirroring `src/opengl/`

Platform files. These are the only ones that include `<windows.h>`, and they
are added to the allowlist in `check-tree.ps1` (lines 200–244):
- `glide_dll.c`: DLL entry, the exports, the global context (Glide 2 has a
  single context), and the log `C:\V9XDIAG\V9XGLIDE.LOG`, modelled on the
  ICD's log in `gl_icd.c`.
- `glide_surface.c`: the DirectDraw device, the exclusive fullscreen flip
  chain, the depth surface, texture surfaces and LFB locks. Open order and
  recovery follow `v9x_gl_device_open`, `v9x_gl_primary_recover` and
  `v9x_gl_device_redescribe` in `src/opengl/gl_surface.c`.

Pure logic, compiled into the host tests:
- `glide_vertex.c`: `GrVertex` to `V9X_R3D_ABI_VERTEX`.
  - Remove the snap bias some titles add to x and y.
  - Flip y when the origin is lower-left.
  - `oow` becomes `rhw`.
  - `sow/oow`, `tow/oow` become `tu`, `tv`, scaled by the bound texture's
    aspect ratio and the 256-texel coordinate range.
  - `ooz` (Z-buffer) or `oow` (W-buffer) becomes `sz`.
  - The float colour channels are packed to ARGB.
  - Fog from the `grFogTable` goes in specular alpha.
- `glide_state.c`: Glide state to `V9X_R3D_ABI_STATE`.
  - Maps the depth function, alpha test, blend factors, cull mode and chroma
    key.
  - Recognises the common `grColorCombine` / `grAlphaCombine` /
    `grTexCombine` patterns (modulate, decal, iterated only, constant only)
    and maps them onto what the interface expresses.
  - An unrecognised combination is logged once per run and drawn with the
    closest mapping.
- `glide_texmem.c`: the TMU address space, which the application manages.
  - Emulated as one linear range (`grTexMinAddress`/`MaxAddress`).
  - A download records its start address, format and LOD range.
  - A download that overlaps an earlier one invalidates it.
  - `grTexSource` resolves an address to a texture record.
  - `grTexTextureMemRequired` and `grTexCalcMemRequired` use Glide's own
    size arithmetic.
- `glide_texfmt.c`: Glide formats converted to the interface's 565, 1555 and
  4444. Only the formats Phase 0 shows NFS II SE using are converted: likely
  565, 1555, 4444, P_8 through `grTexDownloadTable`, and possibly the 8-bit
  intensity and alpha formats. Texture squaring, size fitting and the
  565-to-1555 rewrite already exist in `gl_texture.c` (`v9x_gl_tex_fit`,
  `v9x_gl_tex_square_fill`, `v9x_gl_tex_565_to_1555`). If Glide needs them,
  moving them to a shared module changes external symbols, so that is raised
  before it is done (CLAUDE.md, "C").

### Exports

Glide 2 on Win32 exports `__stdcall` decorated names (`_grDrawTriangle@12`
and so on), and games link against those names.
- A manifest, `src/glide/glide_entrypoints.psd1`, lists the full Glide 2.x
  export set with argument byte counts, in the pattern of
  `gl_entrypoints.psd1`.
- Every export exists from day one. An unimplemented export logs its first
  call and returns a benign default.

### Identity

`grSstQueryHardware` / `grSstQueryBoards` report one Voodoo Graphics (SST-1)
with 1 TMU, 2 MiB of texture memory and 2 MiB of framebuffer.
`grGlideGetVersion` reports 2.4x. Both are named constants citing the
reference manual. If NFS II SE wants different values, Phase 0 shows it.

### Presentation

`grSstWinOpen`:
- `SetCooperativeLevel(EXCLUSIVE|FULLSCREEN)`, then `SetDisplayMode(w,h,16)`
  for the requested `GrScreenResolution_t`.
- A primary with one back buffer per extra colour buffer requested, plus a
  depth surface when aux buffers are requested.
- The render target is the back buffer.

`grBufferSwap(n)` flips and honours the swap interval. `grSstWinClose`
restores the mode and the cooperative level.

The ICD only ever presented windowed, so drawing into a flip-chain back
buffer through the render interface is a new path. Phase 2 verifies it before
anything depends on it. If it fails, the fallback is an offscreen back buffer
plus a `Blt` to the primary, as `v9x_gl_drawable_present` does.

### LFB, only if Phase 0 shows it

- `grLfbLock` / `grLfbUnlock` map to `Lock` on the chosen buffer; the HAL's
  `Lock` already drains the engine.
- A 565 write mode goes direct. Other write modes and `grLfbWriteRegion`
  convert through a shadow buffer.

### Interface gaps

Anything NFS II SE needs that ABI 4 cannot express (chroma key, a combine
mode, a texture format) is an ABI v5 change in `r3d_abi.h`. That is a design
change: it is listed in the Phase 0 record and agreed before it is coded.

### Defaults

There are no INI keys and no V9X3D verbs (memory: no gated defaults). The DLL
rides on the engine `Direct3D=` already selects.

## Phases

### 0. Census: what NFS II SE calls (netbook)

1. Build the export-complete DLL with every export logging its arguments and
   returning success or a plausible value. This is the scaffold every later
   phase grows. It covers:
   - `scripts/build-glide.ps1`, adapted from `scripts/build-opengl-icd.ps1`:
     `wcc386 -bd -zl`, `nodefaultlibs`, a version resource, and a `wdump`
     audit that imports are only KERNEL32 and USER32 and that every manifest
     export is present.
   - check-tree rules: the OS-header allowlist, and the `src/glide`
     pure-logic files kept free of `d3d_internal.h` and the other HAL
     headers, as rule 489–513 does for r3d.
2. Install NFS II SE on the netbook. Confirm which executable or setting
   selects 3Dfx mode. Copy the stub into the game directory, not SYSTEM;
   Windows loads the application directory first.
3. Run the menus and one race. Collect `V9XGLIDE.LOG`.
4. Write a decision record, `docs/decisions/2026-10-xx-nfs2se-glide-census.md`.
   It contains:
   - the calls made, with counts;
   - resolution and buffer counts;
   - texture formats and LOD and aspect ranges;
   - combine modes, depth mode, fog, chroma key;
   - LFB use, and which reads and writes;
   - what the game does on a failure return;
   - the ABI gaps.

   The phases below are trimmed to that list.

**Gate:** `check-tree.ps1` green. The log and the decision record are
committed with the evidence in `docs/probe/netbook-nfs2se-glide-census-<date>/`
(README with machine, boot, and a file table).

### 1. Pure logic with host tests

Write `glide_vertex.c`, `glide_state.c`, `glide_texmem.c` and
`glide_texfmt.c`, each with a test in `tests/host/test_glide_*.c` added to
`scripts/lib/host-sources.ps1`. Each test is written first and seen to fail.
Test cases come from the census values: real vertex snapshots, real download
addresses, real combine calls.

**Gate:** `build-host.ps1` and `run-checks.ps1` green.

### 2. Fullscreen, clear and swap, plus a probe

- `grSstWinOpen`, `grBufferClear`, `grBufferSwap`, `grSstWinClose`, depth
  buffer setup.
- A probe, `tools/diag/glide_probe_win32.c` built by
  `scripts/build-glide-probe.ps1`, modelled on `r3d_probe_win32.c`. It links
  only to our `glide2x.dll`, draws known flat, Gouraud, textured, blended and
  depth-tested triangles, `Lock`s the back buffer, and writes the pixels it
  reads to a log or BMP. That gives an answer independent of screenshots,
  which go blind during flips.

**Gate:**
- The probe's readback matches between the software-engine guest
  (`Win98SE-Fast-D3D`, port 9878) and the netbook. Differences are listed
  and explained.
- The desktop is intact after `grSstWinClose` on both.
- A decision record covers the flip-chain-as-render-target question.

### 3. Triangles and textures

- `grDrawTriangle`, `grDrawPlanarPolygon[VertexList]`,
  `grDrawPolygon[VertexList]`, plus lines and points if the census lists
  them.
- Batching into `V9X_R3D_ABI_BATCH_MAX` (64), held until a state change,
  swap or LFB lock, as the ICD does with `v9x_gl_pending_flush`.
- Texture download into hardware surfaces through the ICD's pattern:
  `v9x_gl_hw_texture` → create or upload, oldest-first eviction, and a CPU
  copy for retry when the engine refuses.
- Fog and chroma key, as the census requires.
- The probe gains textured and palettised cases.

**Gate:** host tests plus probe readback on both targets.

### 4. NFS II SE on the netbook

- Menus, car select, one full race on one track, then a return to the
  desktop.
- Evidence:
  - `V9XGLIDE.LOG`;
  - before and after `V9XTRACE` snapshots;
  - screenshots taken from the menu, which is not flipping, or frame dumps
    from the DLL if the agent is blind;
  - fps if the game reports it. It is recorded, not compared (memory: no
    benchmark chasing).
- Every visible defect is either fixed with a test first, or filed in
  `docs/issues/`.

**Gate:**
- A decision record with captures.
- A probe README in the existing format (machine, boot, file table, results).
- `run-checks.ps1` green.

### 5. Packaging and release notes

- `scripts/build-active-package.ps1` and `scripts/lib/inf.ps1` gain
  `glide2x.dll` in `[SourceDisksFiles]` and the system-directory CopyFiles,
  with the INF self-check extended.
- The 3dfx family is excluded, enforced by a check-tree assertion, so a real
  Voodoo keeps its own Glide.
- The floppy package (`V9XCOPY.BAT`) gets the same exclusion.
- A CHANGELOG entry goes under "Unreleased" in a new "Glide" section, naming
  the machines measured and those not run.

**Gate:** `run-checks.ps1` and a package build.

Later, separate items: the other engines (Rage XL, SiS 6326, Rage IIC,
ViRGE) are each a measurement run, not new code (memory: same design, same
fix). Before they run, any refused-draw policy has to be settled, because
only Gen3 falls back to the software engine. Glide 3 would be another
milestone.

## Hazards already known

- **Netbook IP.** 10.0.1.248 is wired and 10.0.1.254 is wifi. Use whichever
  answers. GUI applications need `exec -Detach`.
- **Agent screenshots** fail while an exec slot is busy, and go blind during
  flips (memory). Plan on probe readback and in-DLL frame dumps rather than
  screenshots.
- **A crashed fullscreen application** may hold `V9XHAL.DLL` until reboot,
  as Final Reality does. Deploy with WININIT renames, never deletes.
- **W-buffer to Z precision.** If NFS II SE uses a W-buffer, mapping it to
  Gen3's Z format can z-fight. The precision is measured on the probe and
  recorded, not guessed.
- **Shutdown.** Glide titles may call `grGlideShutdown` without
  `grSstWinClose`. The shutdown path must restore the display mode either
  way.

## Verification summary

- Each phase ends at a named gate: `check-tree.ps1`, then `build-host.ps1`,
  then `run-checks.ps1`, then evidence from a guest or the netbook. Nothing
  is reported as working on code reading alone.
- End to end: NFS II SE races on the netbook through `glide2x.dll` and the
  Gen3 engine, with log, captures and a decision record committed. The
  desktop survives the exit.
