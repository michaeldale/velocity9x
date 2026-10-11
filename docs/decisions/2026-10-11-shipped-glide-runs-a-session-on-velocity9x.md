# 3dfx's shipped Glide 3 runs a session on Velocity9x and gives the desktop back

Date: 2026-10-11. Machine: A8U4I5, boot 419. Card: retail Voodoo4 4500 AGP.
DLL: 3dfx `GLIDE3X.DLL` 3.10.00.0658 from `C:\WINDOWS\SYSTEM`, driven by
`V9XGL3P.EXE`. Build: branch `vsa100-tier0` on `3d5e6ec` plus this commit.
Evidence: [step2-boot419](../probe/a8u4i5-voodoo4-glide-2026-10-11/step2-boot419/).
Plan: [3dfx-vsa100-glide-ext-hwc.md](../plans/3dfx-vsa100-glide-ext-hwc.md),
step 2.

## What was answered

`dd16.c`'s `Control` passes `EXT_HWC` to `tdfx_hwc.c` (host-tested):
`GETDEVICECONFIG` (vendor `121A`, device `0009`, `fbRam` 33,550,336, one
master chip), `GETLINEARADDR` (BAR0's first 32 MiB through a new mini-VDD
map, function 1Eh; BAR1 as the framebuffer mapping the driver already has),
exclusive take and release (resStatus 1, otherwise not acted on),
`SHARE_CONTEXT_DWORD` (-1, Glide's "no"), `UNMAP_MEMORY` (acknowledged),
`PCI_OP` (acknowledged, not performed). Everything else declines.

## Measured

`V9XGL3P.INI`: `Result=SESSION`, every step reached:
`grGlideInit`, `grSstSelect`, `grGetString(GR_HARDWARE)` = "Voodoo4 (tm)",
`grSstWinOpen` 640x480 returned context `10090B0C`, `grBufferClear`,
`grBufferSwap`, `grSstWinClose` returned 1, `grGlideShutdown`.

The escapes, from `V9XHWC.INI`, in order: `QUERYDEVNODE`, `GETDEVICECONFIG`
(sub-function 2) twice, `GETLINEARADDR` (3), `SETEXCLUSIVE` (8),
`SHARE_CONTEXT_DWORD` (15h), `PCI_OP` (18h) six times with offsets `8C`,
`90`, `94`, `80`, `88`, `88`, `RLSEXCLUSIVE` (9), `UNMAP_MEMORY` (16h)
twice. This is the open tree's predicted single-chip order, the repeated
`88` included.

Afterwards: the agent screenshot is the desktop at 640x480x16, drawn
correctly; `V9XGDI /auto` PASS. The machine answered throughout.

## What this shows

1. **The six-call `EXT_HWC` subset, plus `QUERYDEVNODE`, is enough** for a
   single-chip Glide 3 session with 3dfx's own DLL.
2. **Mini-VDD mappings work as Glide's register and framebuffer pointers**
   in a separate process, as they do for the HAL.
3. **Leaving `PCI_OP` unperformed and exclusive unenforced did not stop the
   session or the restore.**

## Not established

- **What the monitor showed.** Glide drives the video registers itself; the
  screenshot reads the framebuffer through GDI after the restore. Whether
  the cleared blue frame appeared was not watched.
- Whether GDI or DirectDraw drew to the chip while Glide held it (step 3).
- Glide 2, a real game, a killed Glide process, `grSstVidMode`.
