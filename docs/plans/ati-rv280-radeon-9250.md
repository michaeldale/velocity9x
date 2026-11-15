# Radeon 9250 / 9200SE (RV280) in the ati family

Date: 2026-11-15

Status: phases 0 to 4 done on branch `ati-rv280`; phase 5's Direct3D
HAL passes V9XD32. The `rv280` row runs
M-HNBJNV's 9250 (boot 19) with DirectDraw fill, copy and flips on the
hardware, GDI in the screen shadow, and VBE modes.
- **Phase 2:** the scanout sits at GPU address 0, the card has 128 MiB,
  and the CP is off
  ([record](../decisions/2026-11-15-rv280-register-probe.md)).
- **Phase 3:** the RS690 builder is byte-exact here with the pre-R300
  flush, and V9XDDP runs on the engine
  ([record](../decisions/2026-11-15-rv280-2d-write-probe.md)).
- **Phase 4:** flips go through `CRTC_OFFSET` under `OFFSET_LOCK`, as
  Linux's `r100_page_flip` does. They are taken at the blank at 8, 16
  and 32 bpp, 23 of 23 by the time each was called done
  ([record](../decisions/2026-11-15-rv280-flips.md)).

- **Phase 5, first step:** the R200 3D engine draws by register writes
  with the CP off. Flat and Gouraud triangles are right at 16 and 32 bpp,
  a D3DCOLOR goes in as it is, and the 2D engine is still exact afterwards
  ([record](../decisions/2026-11-15-rv280-3d-draw-probe.md)).
- **Phase 5, textures:** texture unit 0 as `R200TextureSetup` writes it.
  Decal and wrap are exact, modulate is within a step, and bilinear is
  within 0.75 of a step, at 16 and 32 bpp
  ([record](../decisions/2026-11-15-rv280-texture-probe.md)).

- **Phase 5, Z, blend and alpha test:**
  - Z16 LESS with writes is right with vertex Z in 0-1. The Z buffer is
    stored tiled, and its layout is not decoded.
  - SRCALPHA / INVSRCALPHA blending is exact.
  - The alpha test is right.
  - Alpha needed `SE_CNTL`'s alpha shade model set; left SOLID, it read 0.

  ([record](../decisions/2026-11-15-rv280-z-blend-probe.md)).

- **Phase 5, cull, perspective, fog, specular:** all are right.
  - The hardware's clockwise is the screen's.
  - Perspective needed `RE_CNTL`'s `PERSPECTIVE_ENABLE`, which the X
    driver's init clears.
  - Fog comes from the specular alpha.
  ([record](../decisions/2026-11-15-rv280-cull-rhw-fog-probe.md)).

- **Phase 5, the Direct3D HAL:** `d3d_rv280_map.c` / `d3d_rv280.c`, the
  RS690's split, with `D3D` in the row's EngineCaps. V9XD32 passes at 16
  and 32 bpp on boot 22. Five texture formats, no mipmaps, Z tiled with
  whole-surface fills only
  ([record](../decisions/2026-11-15-rv280-d3d-hal.md)).
- **Phase 5, the render interface:** scissor and write mask measured, and
  the colour pitch found to need whole 32 bytes. The ICD and Glide draw
  on the R200: V9XGLP and V9XGLIDP pass, with GeomScan's band open
  ([record](../decisions/2026-11-15-rv280-render-interface.md),
  [issue](../issues/2026-11-15-rv280-gl-geomscan-band.md)).

- **Phase 5, first applications:** 3DMark 2000's default benchmark runs
  to its score (905) with no refusals or timeouts, and its grabbed frames
  look right. The Quake 2 demo runs on our ICD at 54.7 fps in
  `timerefresh`, at 640x480 windowed
  ([record](../decisions/2026-11-15-rv280-3dmark2000-quake2.md)).

Next in phase 5:
- GeomScan's band;
- mipmaps, the colour key, and partial depth fills.

Target: the RV280 as a new generation in the one `ati` family, module
tree `src/chipsets/ati/rv280/`.
- **Cards:**
  - PowerColor Radeon 9250, `1002:5960`, subsystem `148C:2094`;
  - ECS Radeon 9200SE, `1002:5964`, subsystem `1019:4C43`.

  Both were surveyed
  ([record](../decisions/2026-11-13-rv280-radeon-9200se-9250-survey.md)).
- **Test machine:** M-HNBJNV (VICO98): Pentium III 1 GHz on a VIA PM133,
  512 MiB, Windows 98 SE in Spanish. It is reached through the
  remote-help relay, and the 9250 is fitted.
- **Goal:** "full 2D/3D support" (Michael, 2026-11-15): DirectDraw on
  the 2D engine, flips, then Direct3D on the R200 3D engine, then
  OpenGL and Glide through the ICD and Glide layers as on the other
  chips.

## Sources

- **Linux `radeon`, in `build\external\rs690\linux-radeon\`:** `r100.c`,
  `r100d.h`, `radeon_reg.h`, `radeon_legacy_crtc.c`. RV280 is
  `CHIP_RV280`, an R200-class part, so Linux runs it through the r100 and
  r200 paths.
- **xf86-video-ati 6.14.6 (UMS), in
  `build\external\rs690\xf86-video-ati-6.14.6\`:**
  - 2D by MMIO: `radeon_accel.c`, `radeon_accelfuncs.c`;
  - the memory map: `radeon_driver.c:1505-1535`;
  - the R200 Render path by MMIO: `radeon_exa_render.c`, `R200*`
    functions, with vertex dwords written to `SE_PORT_DATA0`.
- **Still to fetch, for phase 5:** Mesa's classic `r200` driver from
  Mesa 20.x (`src/mesa/drivers/dri/r200/`), the full R200
  fixed-function state reference.

## What the sources settle

| | RV280 per the sources | Consequence |
|---|---|---|
| Register file | Legacy Radeon (r100); BAR2 `E5000000` on the 9250 (survey) | The 2D register offsets are the RS690 builder's: `DP_GUI_MASTER_CNTL` 146Ch, `DST_PITCH_OFFSET` 142Ch, `SRC_PITCH_OFFSET` 1428h, `DST_Y_X` 1438h, `DST_HEIGHT_WIDTH` 143Ch |
| 2D cache flush | `RB3D_DSTCACHE_CTLSTAT` 325Ch, `RB3D_DC_FLUSH_ALL` 0Fh, busy bit 31, for every family up to RV280 (`radeon_accel.c:195-210`) | **Not** the R300+ `DSTCACHE_CTLSTAT` 1714h (flush 5) that the RS690 builder uses. The flush is the one chip-specific value in the 2D path |
| Idle | `RBBM_STATUS` 0E40h, bit 31 active, FIFO count in bits 6:0 | As on the RS690 |
| GPU address of VRAM | `MC_FB_LOCATION` 0148h. The base is `(value & FFFFh) << 16` on pre-R600 parts (`radeon_driver.c:1528-1530`) | `DST_PITCH_OFFSET`'s offset field is a GPU address: read it, never write it (the BIOS has POSTed) |
| Memory | `CONFIG_MEMSIZE` 00F8h. VBE reports 16 MiB | The real size is read in phase 2. The heap stays at VBE's 16 MiB until it is measured |
| Display | Legacy CRTC: `CRTC_OFFSET` 0224h (bit 30 `GUI_TRIG_OFFSET`, bit 31 `OFFSET_LOCK`), `CRTC_OFFSET_CNTL` 0228h, `CRTC_PITCH` 022Ch, `CRTC_STATUS` 005Ch | Flips by `CRTC_OFFSET`, vblank by `CRTC_STATUS`. Pre-AVIVO, so not the RS690's `D1GRPH` |
| Mode setting | VBE 2.0, 50 linear modes, no CRTC block | Keep `vbe-lfb`, as on every ATI row since the Rage 128 |
| Second head | Function 1 (`1002:5940` / `5D44`, class 0380) | Never claimed. The one-display guard (19ec1531) covers a forced install |
| 3D | R200: fixed-function texture blending (`PP_TXCBLEND`), TCL that Direct3D's pre-transformed vertices bypass. Vertex dwords can go to `SE_PORT_DATA0` without the CP (xf86-video-ati `ACCEL_MMIO`) | Direct3D by register PIO, as on the RS690, without microcode or a ring |

## Phases

0. **Baseline (done).** `vbe` package, 1024x768x16, `Acceleration=none`,
   screen shadow on.
1. **Identity.** Claim `1002:5960` and `1002:5964` in the `ati` manifest
   as a row with no engine, VBE modes, and the screen shadow, so the INF
   names the card and the row exists to hang the engine on. Other RV280
   ids (`5961`, `5962`, `5965`) wait for a survey of one.
2. **Read-only register probe** on the 9250. Read:
   - `CONFIG_MEMSIZE`, `MC_FB_LOCATION`, `RBBM_STATUS`, `HOST_PATH_CNTL`
     and `SURFACE_CNTL`;
   - the 2D state block, only while idle;
   - `CRTC_OFFSET`, `CRTC_OFFSET_CNTL`, `CRTC_PITCH` and `CRTC_GEN_CNTL`
     at the running mode.

   This proves the GPU address of the VBE scanout and the real memory
   size.
3. **2D engine.** A write probe first: a fill and a screen-to-screen
   copy at 8, 16 and 32 bpp, overlaps both ways, byte-exact readback.
   Then the engine32 `fill` / `copy` / `wait_idle` ops for DirectDraw.
4. **Flips and vblank** through `CRTC_OFFSET` with `GUI_TRIG_OFFSET`,
   and `CRTC_STATUS`.
5. **3D by PIO on R200.** A probe first: one flat triangle through
   `SE_VF_CNTL` / `SE_PORT_DATA0` with the texture environment passing
   the vertex colour, read back. Then textures, Z, blend, alpha test,
   fog, and the HAL glue in the RS690's split: a host-tested state map,
   then the HAL. OpenGL and Glide follow.
6. **Heap beyond 16 MiB** once `CONFIG_MEMSIZE` is measured.

## Unknowns that need a measurement

- Whether the 2D engine is usable as POSTed under VBE, or needs Linux's
  `RBBM_SOFT_RESET` sequence first.
- Whether VBE mode sets disturb engine state.
- Whether the DOS box leaves engine and scanout state alone.
- PIO throughput on a 1 GHz Pentium III over AGP.

## Decisions

- 2026-11-15, Michael: "work on full 2D/3D support" for the 9250, after
  the second-head fault was fixed.
