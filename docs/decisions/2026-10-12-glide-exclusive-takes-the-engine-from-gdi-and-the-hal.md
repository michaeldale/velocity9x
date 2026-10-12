# Glide's exclusive takes the Voodoo4's engine from GDI and the HAL, and gives it back

Date: 2026-10-12. Machine: A8U4I5, boot 423. Card: Voodoo4 4500 PCI
(121A:0009 subsystem 0005121A rev 01, bus 0 device 9), swapped in for the
AGP card on boot 421. DLL: 3dfx `GLIDE3X.DLL` 3.10.00.0658, driven by
`V9XGL3P.EXE`. Build: `vsa100-tier0` on `be6ac29` plus this commit (DRV);
HAL and mini-VDD from `be6ac29`. Evidence:
[step3-boot423](../probe/a8u4i5-voodoo4-glide-2026-10-11/step3-boot423/);
the PCI card's first boot on Velocity9x (boot 422) is in
[a8u4i5-voodoo4-pci-2026-10-12](../probe/a8u4i5-voodoo4-pci-2026-10-12/).
Plan: [3dfx-vsa100-glide-ext-hwc.md](../plans/3dfx-vsa100-glide-ext-hwc.md),
step 3.

## What changed

On `HWCSETEXCLUSIVE` the driver now:

- waits for the engine to go idle and sets a GDI hold, so every GDI
  primitive declines at gate 1 and the DIB Engine draws instead
  (`v9x_gdi_accel_hold`, `gdi_accel.c`);
- clears `V9X_DD_ENGINE_VALID` in the shared block, so the HAL's TDFX
  engine refuses and DirectDraw blits run on the CPU.

On `HWCRLSEXCLUSIVE` both are undone. The HAL gets VALID back only if it had
it at the take. While the hold lasts, any restamp of the flags (a DirectDraw
session setup, `V9xDdInvalidate`) is recorded for the release and withheld
again. That bookkeeping is `v9x_tdfx_hwc_hold_apply` / `_restamp` in
`tdfx_hwc.c`, host-tested: written against a pass-through stub first (4
failures), then implemented.

The plan said to release after Glide's mode set restores the desktop. The
release is at `RLSEXCLUSIVE` instead. Glide sends it after its own idle wait
and before `ChangeDisplaySettings(CDS_RESET)` (vsa100-ext-hwc.md section 3,
steps 18-21), so by then the engine is no longer Glide's. Nothing reaches
the driver after the mode set that would mark a later point.

## Measured

`V9XHWC.INI`, recorded by the escape handler at three points:

| Point | HAL engine flags | GDI fills | GDI copies | GDI declined at gate 1 |
|---|---|---|---|---|
| Just after take | `00000000` | 38 | 1 | 0 |
| Just before release | `00000000` | 38 | 1 | 18 |
| Just after release | `00000001` | 38 | 1 | 18 |

GDI made 18 BitBlt calls during the session, and all 18 went to the DIB
Engine. Fills and copies stayed flat. The HAL's VALID bit was off for the
whole session and on again afterwards. The session itself was the same as
before: `Result=SESSION`, "Voodoo4 (tm)", `grSstWinClose` returned 1.

Afterwards, both clients had the engine again:
- `V9XGDI /accel` PASS: 160 fills and 131 copies on the engine, no idle
  timeouts, and the declined-at-gate-1 count still 18.
- `V9XDDP` COMPLETE, fill pixel check OK.
- The HAL snapshot (`V9XSNA7.INI`, HAL `0.16.1 be6ac29`) reads engine
  flags 9 (VALID and status-validated), type 8 (TDFX), no timeouts, and
  `CountBltEngine=6`.
- `V9XGDI /auto` PASS, and the desktop screenshot is correct.

`PCI_OP` is still acknowledged and not performed. The six config registers
Glide clears at close (80h, 84h, 88h, 8Ch, 90h, 94h) read 0 on this card
under Velocity9x after the session (`VSA100-after.TXT`). They also read 0
on the AGP card under 3dfx's driver (boot 407). On a single chip without
AA, the six writes of 0 would change nothing.

## Not established

- **A killed Glide process** never sends `RLSEXCLUSIVE`, so the engine
  stays withheld until reboot. The desktop should stay correct and only get
  slower, but that has not been tried.
- **CPU drawing during the hold.** GDI and DirectDraw can still draw into
  the framebuffer with the CPU while Glide holds the card. With Glide's
  buffers above the desktop this should not show. It has not been watched
  on the monitor.
- Glide 2, a game, and AA (whose open-time `PCI_OP` writes are non-zero).
