# The Voodoo4's desktop moves above Glide's memory, and Diablo II exits cleanly

Date: 2026-10-12. Machine: A8U4I5, boots 425-427. Card: Voodoo4 4500 PCI
(subsystem 0005121A). Build: `vsa100-tier0` on `8a3529e` plus this commit
(DRV, mini-VDD, HAL). Evidence:
[step4-boot427-desktop-at-15mib](../probe/a8u4i5-voodoo4-glide-2026-10-11/step4-boot427-desktop-at-15mib/).
Problem: [2026-10-12-glide-cmdfifo-overlaps-the-desktop.md](../issues/2026-10-12-glide-cmdfifo-overlaps-the-desktop.md).

## Decision

Michael chose 3dfx's own arrangement: the desktop goes above the memory
Glide is given. The linear memory is split in two, by
`v9x_tdfx_vsa100_desktop_offset` (host-tested; tests written red first, 10
failures):

- **Desktop offset:** half the linear memory, rounded down to a MiB. On the
  Voodoo4 that is 15 MiB.
- **Glide's fbRam:** the memory below the desktop, less a 1 MiB guard:
  14 MiB.
- **DirectDraw heap:** from the desktop to the end of the linear view,
  17 MiB less a page.

A new hook appended to `V9X_HW16_OPS`, `place_desktop`, runs after every
mode set: on Enable and on return from a full-screen DOS box. Through BAR2
it writes four registers:

1. `lfbMemoryConfig` back to the BIOS's `3FFF`;
2. `vidProcCfg` with the desktop on, the overlay off, and linear;
3. the desktop stride;
4. `vidDesktopStartAddr`, which is read back.

Enable adds the offset to the aperture it maps, so the screen PDEVICE and
DirectDraw start at the desktop without knowing it. The 2D engine
addresses video memory from byte 0, so it adds the offset back: GDI asks
`tdfx_hw16.c`, and the HAL takes it from the physical base modulo BAR1's
128 MiB. Glide's `GETLINEARADDR` gets BAR1 from byte 0 through a new
mini-VDD mapping (function 1Fh).

The guard exists because Glide moves the tile boundary down to its own
buffers. The CPU then reaches everything above that boundary through the
tiled view, and a tiled access lands within a row of tiles of its linear
address. Neither the guard's size nor the 15 MiB split is measured to be
the best choice.

## Why the register writes, and not only the start address

- **The tile boundary.** After a clean Glide session, `lfbMemoryConfig`
  is still Glide's (`A7D3C`, page `1D3C`; step 3 capture, boot 423), and
  the VBE mode set does not restore it. With the desktop at 15 MiB that
  boundary would have put the DirectDraw heap in the tiled view. It
  already did, unnoticed, with the old layout: the heap reached 32 MiB.
- **The video processor.** At boot 425 a Glide session that never closed
  survived a reset and our mode set: the overlay was still on, the desktop
  layer off, and the stride was Glide's tiled 10. The monitor did not show
  our desktop, and Michael had to reset again.

## Measured

**Boot 427 at the desktop:**
- `DesktopOffset=15728640`, `DesktopPlaced=yes`, `GlideFbBytes=14680064`.
- `VddReserve vram=17821696`, the heap.
- `vidDesktopStartAddr=00F00000`, `vidProcCfg=30040C81`, stride `500h`,
  tile control `3FFF`.
- `V9XGDI /auto` PASS. `/accel` PASS: 160 fills and 131 copies on the
  engine, pixels compared.
- Michael, at the monitor: a normal desktop.

**Diablo II (`-3dfx`, 3dfx `GLIDE3X.DLL`):**
- Glide opened and closed several times and the game returned to the
  desktop. Michael saw the menus and the town draw correctly and the exit
  return to Windows.
- After the exit: status `5Fh` (idle), command FIFO disabled with no depth
  and no holes, last read pointer `9B8A8h` (inside Glide's 14 MiB), and the
  desktop at `00F00000`, linear, overlay off.
- Before this change, this exit hung the machine twice (boots 423, 424).

## Not established

- **Performance.** Michael thought Diablo II might be slower. Not
  measured. One candidate is texture room: Glide now has 14 MiB rather
  than 32.
- **The guard.** Whether 1 MiB is needed, or enough.
- **Other paths:** Glide 2, Rollcage, NFS II SE, a DOS box during a Glide
  game, and other card memory sizes.
- **Whether `place_desktop` runs on every route back from a DOS box.** It
  runs on the tier-0 `V9xHardwareReset` path; a family with
  `post_mode_set` skips it, and no such family has the hook.
