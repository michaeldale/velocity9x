# OpenGL and Glide on the HD 4000 mobile: the desktop's results, unchanged

Date: 2026-11-05
Status: **measured once per probe**, boot 5, 1366x768x16. OpenGL
(V9XGLP), the render interface (V9XR3DP), Glide 2 (V9XGLIDP) and Glide 3
(V9XGL3P) give W9QI4E14's results on W9QI75E0.

Captures: `docs\probe\w9qi75e0-ivb-mobile-gl-glide-2026-11-05\`. Probes
built at `dba9d510`; the installed driver is `aa70a3a9`
(`2026-11-05-w9qi75e0-ivb-mobile-intel-gma.md`).

| Probe | Result |
|---|---|
| V9XGLP (ICD) | **PASS**, `LifetimeFailures=0`. Vendor Velocity9x, version 1.1.0, renderer "Velocity9x HD 4000" |
| V9XR3DP | `COMPLETE`, `ClearOk`, `DrawOk`, `MaskedOk` 1, `TexturedScissorOk` 0. Every key except `Build` and `DescribeGeneration` equals W9QI4E14's boot 9 run (`boot9-V9XR3DP-16bpp.INI`), including the textured draw under a half scissor being refused, the designed Haswell-path answer |
| V9XGLIDP, the package's `GLIDE2X.DLL` in `C:\V9XGLIDE` | **PASS, failures 0** |
| V9XGL3P, `SYSTEM\GLIDE3X.DLL` | `Result=SESSION Step=done` |

The 32 bpp desktop was not run: on W9QI4E14 the ICD offers no 32-bit
pixel format there and V9XGLP measured Microsoft's renderer.

## Not measured

- 3DCIF: no 3DCIF title on this machine.
- **A DOS box.** The agent refuses to start a DOS VM (`allow_dos_vm=0`),
  and DOS programs run through the agent have stopped Windows restarting
  itself on other machines. The return from a full-screen DOS box needs
  the owner at the keyboard.
