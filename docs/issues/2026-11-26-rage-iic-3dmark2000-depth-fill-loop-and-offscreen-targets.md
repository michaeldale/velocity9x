# Rage IIC: 3DMark 2000 stops in a loop of depth fills, and V9XD32's off-screen render targets stay empty

Date: 2026-11-26. Machine: A8U4I5, Rage IIC AGP (`1002:4757`, 4 MiB),
the 0.61.0 tree's `ati` set, boot 704. Status: open, cause not looked
at. First V9XD32 and first 3DMark 2000 run on this card.

## 3DMark 2000 stops

The default benchmark at 640x480x16 (16-bit textures and Z, triple
buffer; the default 1024x768x32 needs 16 MB). After 1,340 flips it
stopped presenting: three snapshots over 80 s show `FlipHandled`,
`M64Draws` (328,490) and every draw counter still, while `CountBlt`
went 20,335 to 48,375 and the trace ring is nothing but `Blt` entries
with flags `02000000` (`DDBLT_DEPTHFILL`) and exit 1 (handled). 3DMark
was down to one thread; the screen was black but for one white streak
of rotor
([screen](../probe/rageiic-1.0-2026-11-26/3dmark-640-stall/screen-while-stuck.png)).
Windows and the agent stayed responsive, and killing 3DMark returned
the desktop at 800x600. Before the stop the engine refused 7,069 draws
(`M64Refused` 7 to 7,076, last reason 7, texture format).

So 3DMark re-issues a depth fill without end. Whether our depth fill
reports success while leaving something 3DMark checks (the Z surface's
contents, a lost surface) is not known.

## V9XD32 T4 and T16

Both draw into an off-screen render target of their own. T4's clear
returns `E_NOTIMPL` (`T4ClearHr=80004001`) and 990 of 3,861 judged
pixels are wrong; T16 (the DirectX 7 device on its own target) reads 0
for its clear, textured and untransformed pixels with every call
succeeding. On the Rage IIC, off-screen render targets do not receive
the clear or the draws.

Evidence: [`../probe/rageiic-1.0-2026-11-26/`](../probe/rageiic-1.0-2026-11-26/)
(`rageiic/v9xd32-bpp16-V9XD32.TXT`, `3dmark-640-stall/`). Record:
[decision](../decisions/2026-11-26-1.0-rageiic-a8u4i5.md).
