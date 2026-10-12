# Diablo II, Rollcage and NFS II SE run on 3dfx's own Glide, once our mode set stops clearing memory

Date: 2026-10-12. Machine: A8U4I5, boots 427-428. Card: Voodoo4 4500 PCI.
DLLs: 3dfx `GLIDE3X.DLL` 3.10.00.0658 and `GLIDE2X.DLL` 2.61.00.0658. Each
game folder's Velocity9x wrapper is saved beside it as `glide3x.v9x` /
`glide2x.v9x` (and on the host). Michael watched the monitor throughout.
Evidence:
[step4-boot427-428-games](../probe/a8u4i5-voodoo4-glide-2026-10-11/step4-boot427-428-games/).
Plan: [3dfx-vsa100-glide-ext-hwc.md](../plans/3dfx-vsa100-glide-ext-hwc.md),
step 4.

## Boot 427: Rollcage's WM_CLOSE left the FIFO jammed

Rollcage (Glide 3) drew its race correctly. After WCLOSE (WM_CLOSE to its
three windows):

- **Glide never released:** the escape log stayed at "taken".
- **The desktop came back** and responded, scanned out from 15 MiB.
- **Command FIFO 0 was jammed.** It was still enabled (`1FE`), with
  status busy (`0A5F`), read pointer `791C0`, write limit `117000` (the
  FIFO's last byte), and hole count `3FBDB` (261,083 words).

The NFS II SE demo (Glide 2) then opened a session but showed a black
screen. It had set up its display (overlay on, tile boundary below 14 MiB),
but the FIFO registers were exactly Rollcage's. It needed a reset.

**Cause.** The game restores the desktop mode before Glide has turned its
FIFO off, and that mode set runs our Enable. The tier-0 VBE 4F02h call did
not set VBE's don't-clear bit, so the BIOS cleared video memory from offset
0, through Glide's enabled FIFO (18000h-117000h).

What fits: the write limit at the FIFO's very end, and Glide unable to
close afterwards. The clean path fits too: Diablo II's ordinary exit, where
Glide disables its FIFO before the mode set, had no jam. That the BIOS
clears from offset 0 was not measured directly; the result of the fix is
the evidence.

## The fix

`tdfx_hw16.c` passes `V9X_HW16_VBE_LINEAR | V9X_HW16_VBE_NO_CLEAR`. The
desktop is repainted after every mode set, so nothing relied on the clear.

## Boot 428, with the fix (driver hash `984101FC...`)

| Game | API | Monitor (Michael) | After WM_CLOSE |
|---|---|---|---|
| Rollcage demo | Glide 3 | race correct | released; FIFO off, no holes; idle |
| NFS II SE 3Dfx demo (`C:\NFS2DEMO`) | Glide 2 | race correct | released; FIFO off, no holes; idle |
| NFS II SE full (`GLIDE2X.DLL` from SYSTEM) | Glide 2 | race correct | released; FIFO off, no holes |

After the full NFS II SE exit:
- **Status** read `25F` (busy) once. It was a single sample, and the
  `/accel` run straight after had no idle timeouts.
- **Registers:** desktop at `00F00000`, linear, overlay off, tile control
  `3FFF`.
- **`V9XGDI /accel`:** PASS, with 160 fills and 131 copies on the engine.

Diablo II (Glide 3) ran on boot 427 with the desktop above Glide
([2026-10-12-voodoo4-desktop-above-glide.md](2026-10-12-voodoo4-desktop-above-glide.md)).
Its exit was clean before this fix, because Glide disabled its FIFO first.

## Not established

- **A process that dies before Glide closes.** Glide's state stays, and the
  driver keeps the engine withheld until the next session or a reboot.
- **Performance** under 3dfx's Glide (14 MiB of texture room), and any
  comparison with our wrappers.
- **Diablo II on the boot 428 build.**
- **Other Glide titles**, and AA settings.
