# Ivy Bridge OpenGL and Glide on W9QI4E14: the HD 4600's results, and a renderer name fixed

Date: 2026-10-21
Status: **measured once per probe.** OpenGL (V9XGLP), the render interface
(V9XR3DP), Glide 2 (V9XGLIDP) and Glide 3 (V9XGL3P) give the same
results on the HD 4000 as on the HD 4600. The one difference was the
renderer name, which read "HD 4600" and is fixed (`e97fc810`). These
probes come after step 6 of `docs\plans\intel-ivybridge-gen7.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-gl-glide-2026-10-21\`. Probes
built at `a4754d0f`; V9XR3DP rebuilt at `e97fc810`.

## Boot 8 (driver `178201b9-dirty`, 640x480x16)

| Probe | Result |
|---|---|
| V9XGLP (ICD, `C:\V9XDIAG`) | **PASS**, every `Ok=1`. Vendor Velocity9x, version 1.1.0, `GL_SGIS_multitexture`. Renderer **"Velocity9x HD 4600"**, which is wrong (`V9XGLP-16bpp.INI`) |
| V9XGLIDP, tree `GLIDE2X.DLL` in `C:\V9XGLIDE` | **PASS, failures 0**, all 21 checks: clears and flips, triangle, W-buffer reject and accept, clip window, line, snap, 565 and palette textures with a palette change, chroma key on and off, half blend `8410`, table fog (`V9XGLIDP.INI`) |
| V9XGL3P, `SYSTEM\GLIDE3X.DLL` | `Result=SESSION Step=done`: open, clear, swap and close with no Glide error (`V9XGL3P.INI`) |

The HAL snapshot after the three (`after-gl-glide-V9XSNA7.INI`) compared
with the one after 3DMark 2000 on the same boot:

- `AsyncSubmits` 1,667,063 → 1,667,081 and `SyncSubmits` 7,439 → 7,603,
  all on the render ring;
- `R3dDrawCalls` 178;
- no breadcrumb, ring-space, flip-wait or engine timeouts, and
  `EngineResets` 0;
- no watch fault for V9XGLP or V9XGLIDP;
- builds agree.

Haswell's render path has no software fallback, so a draw the engine
did not take would have been refused and counted. V9XGLIDP's pixels read
back exact.

## The renderer name

`d3d_core.c` named the `INTEL_GEN75` engine "Velocity9x HD 4600" for
every chip on it. It now reads `V9X_DD_ENGINE_CAP_GEN7_IVB` and says
"Velocity9x HD 4000" on Ivy Bridge. run-checks passed. Only `V9XHAL.DLL`
changed (472,576 bytes). It was put to `C:\V9XNHAL.BIN` and renamed over
the installed one by a plain `WININIT.INI` pair, with no `NUL=` line,
then `reset` (boot 9). The other installed files stay at `178201b9`.

## Boot 9 (HAL `e97fc810-dirty`)

| Probe | Result |
|---|---|
| V9XGLP, 640x480x16 | **PASS**, every `Ok=1`, renderer **"Velocity9x HD 4000"** (`boot9-V9XGLP-16bpp.INI`) |
| V9XR3DP, 640x480x16 | COMPLETE: engine 9, targets `02h`, textures `1Ah`, renderer "Velocity9x HD 4000". `ClearOk`, `DrawOk` and `MaskedOk` 1; every refusal returned its own code and left the target alone. The textured draw under a half scissor is refused (result 4), the designed Haswell answer. Every key except the build, the generation and the name equals the 4570's 0.21.0 run (`hsw-4570-0.21.0-probes-2026-10-20\V9XR3DP.INI`) |
| V9XGLP, 640x480x32 | PASS, but renderer **"GDI Generic"**: the ICD offers no 32-bit pixel format, as on the Matrox (`2026-10-18-mga2164w-software-fallback.md`). This run tested Microsoft's renderer, not ours |
| V9XR3DP, 640x480x32 | COMPLETE, targets `20h`, renderer "Velocity9x HD 4000". The probe's oracle reads 16-bit words, so its colour checks read 0 at 32 bpp and mean nothing. The depth raws (`8000`) are right. The Mystique's 32 bpp run reads the same way |

The desktop was put back to 640x480x16 with `V9XMSW /set:640x480x16`.
`V9XMSW.EXE` is in `C:\V9XPKGI` on this machine, not `C:\V9XDIAG`.

## Not measured

- **3DCIF.** Neither the SDK demos nor a 3DCIF title is on this
  machine. On the 4570 the same path draws 3DCIF only without depth, and
  Laputa hangs (`2026-10-18-haswell-glide-3dcif-probes.md`). Nothing here
  says Ivy Bridge differs.
- No OpenGL or Glide game, so no load beyond the probes. Whether i915's
  IVB L3 workarounds matter is still open.
- OpenGL at 32 bpp, for the reason above.

## Boot 10: the package installed whole

The `9882c001` package went in by Have Disk over the boot 9 set, then
`reset`. `WININIT.INI` held four plain renames (the two Glide DLLs,
`V9XDISP.DRV`, `V9XHAL.DLL`) and no `NUL=`. V9XTRACE reads
`DriverBuild=0.23.0 9882c001`, `BuildsAgree=1`. V9XHW.INI keeps
`hardware-gen7` and EngineCaps `00080017`. V9XGLP passes with renderer
"Velocity9x HD 4000". The machine is left on this set at 640x480x16.
