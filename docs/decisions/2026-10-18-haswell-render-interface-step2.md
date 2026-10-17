# HD 4600 render interface, step 2: the mask drawn, the scissor as the drawing rectangle; Half-Life OpenGL on the engine, windowed

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boot 20, the HAL
replaced by WININIT rename. Step 2 of
[intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md),
plus a first Half-Life `-gl` session (step 3's first title). Evidence:
[`../probe/hsw-4570-render-interface-step2-2026-10-18/`](../probe/hsw-4570-render-interface-step2-2026-10-18/).

## What changed

- **Write mask:** an explicit draw's mask is `BLEND_STATE`'s write-disable
  nibble (`v9x_d3d_hsw_write_disable`: R G B to bits 2 1 0; alpha always
  written). Direct3D's draws state no mask and keep writing everything.
- **Scissor:** `v9x_hsw_draw.scissor` makes `3DSTATE_DRAWING_RECTANGLE`
  the scissor, inclusive, with the origin 0. `v9x_d3d_hsw_scissor`
  converts the exclusive right and bottom and clamps to the target. An
  empty scissor draws nothing and reports success. `draw_workload`
  refuses an inverted rectangle or one past the target.
- **Refusals:** the mask and scissor refusals of step 1 are gone; CPU
  levels are still refused.
- **Host tests:** they failed first (16 checks), then passed. They
  include the drawing rectangle's words, worked by hand.

## Measured

**V9XR3DP:**

| | boot 19 | boot 20 |
|---|---|---|
| `DrawMaskedResult` / `MaskedRaw` / `MaskedOk` | 4 (refused) / `07E0` / 0 | 0 / `FFE0` / **1** |
| textured scissor draw | refused | refused: it uses a CPU texture (`r3d_probe_win32.c:362`), correctly refused here; on Gen3 it passes only through the software fallback |
| `ClearOk`, `DrawOk` | 1, 1 | 1, 1 |

**Neither probe exercises a partial scissor on a hardware draw.** The ICD
clips geometry to the scissor box itself (`gl_prim.c`,
`v9x_gl_prim_draw_rect`), and Glide sends the full target. So the
engine's scissor path ran only with the whole target.

**V9XGLP:** `Result=PASS`, no Ok key 0. Its scissored-clear readings
(`ScissorInsidePixel` and `ScissorOutsidePixel`) read magenta, the
earlier clear colour, at boots 18, 19 and 20 alike. So no change here
moved them, and what they should read on this engine is not
established.

## Half-Life `-gl`

- **Full screen: refused.**
  - `hl.exe -console -gl -w 640 -h 480 -full` started the launcher. When
    the engine started, it showed "The selected OpenGL mode is not
    supported by your video card" (`hl-gl-fullscreen-refused.png`).
  - The ICD logged no attach for HL.EXE, so this happens before
    `opengl32` loads it.
  - `EngineGLDriver` was already "Default".
  - 640x480x16 is in the published mode list (`V9XMODES-boot20.INI`,
    `Row02`).
  - **Why Half-Life refuses it is not established.** Its GDI mode test
    (`ChangeDisplaySettings`) is a candidate, not checked.
- **Windowed: drawn on the engine.**
  - `-gl -window` attached the ICD. Half-Life ignored `-w 640 -h 480` and
    drew 400x300 (the ICD's scissor field reads 0,0,400,300).
  - **`timedemo mwd5`: 54.679, 63.871, 63.821 fps, best 63.871**, at
    400x300 in a window. That is not comparable with the netbook's
    640x480 records, and is recorded as what it is.
  - The ICD's census at detach: **332,048 batches and 14,374,469
    triangles on the hardware path.**
    - CPU path 0, hardware-refused 0;
    - 214 video-memory textures, no create failures, no evictions;
    - 1,164 untextured batches (Half-Life's own);
    - 22,138 `alpha-dropped`, as on other engines.
  - The picture was not checked frame by frame. The console overlay was
    readable throughout.

## Later the same boot: full screen draws, 56.9 fps

- **The GDI mode path is not the refusal.** `V9XMSW /enum` (new) walks
  `EnumDisplaySettings` the way GLQuake's `VID_InitFullDIB` does. It
  lists ten modes, including 640x480x16, and every one but the 4 bpp mode
  answers `CDS_TEST | CDS_FULLSCREEN` with 0, successful. A real
  `CDS_FULLSCREEN` switch to 640x480x16 and the restore both succeed and
  read back right (`V9XMSW-enum-boot20.INI`).
- **The same full-screen command then worked.** The command was
  `-console -gl -w 640 -h 480 -full`, after one windowed `-gl` session.
  The ICD attached to HL.EXE and drew on the hardware path with nothing
  refused.
- **`timedemo mwd5` at 640x480x16 full screen: 54.550, 56.894, 56.508
  fps, best 56.894.** The netbook's GMA 950 measured 16.8 fps with `-gl`
  (`2026-10-09-icd-call-census-and-attrib-stacks.md`). The builds, CPUs
  and DirectX runtime differ, as for the Direct3D comparison.
- **Why the first two full-screen starts refused** is not established, and
  is set aside at Michael's word (2026-10-18). What is known:
  - Half-Life's registry settings differed between the failing and the
    working start only in `EngineAllowWindowed`, which the windowed
    session added.
  - Its saved `config.cfg` holds no video mode.
  - A Direct3D session in between changed only `EngineType` (2 to 3).
- **Seen but not investigated:** in the full-screen console, the
  background is dark from about x = 430 to the right edge
  (`hl-gl-fullscreen-console.png`).

## Not done

- Why Half-Life refused full-screen OpenGL twice (set aside), and the
  console background's dark band.
- Glide (step 5) and 3DCIF (step 6); two texture units (step 4).

Gates: check-tree, build-host (16 failures first, then passing),
run-checks.
