# The MGA-2164W's VBE BIOS reports 4 MiB of its 8, and the matrox family believes it

Date: 2026-10-10. A8U4I5 (10.0.1.172), Millennium II MGA-2164W, subsys
1200102B, rev 00.

## Observed

- Boot 380, Velocity9x `matrox` (afb538c): `VbeController=v=0200 mem=64`,
  so 4F00h reports 64 x 64 KiB = 4 MiB. `V9XHW.INI` `VbeVramBytes=4194304`,
  `V9XMODES.INI` `Vram=reported=4194304 usable=4194304`, and V9XDDP's
  off-screen heap at 800x600x16 was 2.9 MiB.
- Boot 383, Matrox's driver 4.33c: its HAL reports 7.0 MiB off-screen at
  the same mode, and `V9XVRAM.EXE` holds all sixteen 512 KiB signatures
  from 0 to 7.5 MiB: `Result=AT-LEAST-8MIB`
  (`docs/probe/a8u4i5-mga2164w-matrox-hal-2026-10-10/b383-V9XVRAM.INI`).
  The tool stops at 8 MiB, so 16 MiB is not excluded.

So the card has at least 8 MiB and its BIOS tells VBE callers 4. The
matrox family sizes memory from 4F00h (the first-boot record notes "the
BIOS's figure"), so half the card is unused: off-screen surfaces now,
and the Z buffer and textures of the
[hardware Direct3D plan](../plans/matrox-mga2164w-hardware-3d.md) later.

## Not known

- Whether the 2064W's BIOS does the same. Its card reported `mem=128`
  (8 MiB) and that matched Matrox's figure, so probably not, but its
  memory was never walked.
- How Matrox's driver sizes it. The 2164W specification has no size
  register; OPTION.productid <28:24> "could encode the amount of memory"
  (3-19) at the board maker's choice. A write-and-wrap walk, as
  `V9XVRAM` does, is the only method that does not depend on the board.

## Next

Size the 2164W from the memory, in host-tested policy with the walk in
the existing backend layer, capped by the 16 MiB aperture; keep 4F00h
as the floor. Phase 0 of the plan.

## 2026-10-10, boot 385: fixed for the matrox family

The family now walks its memory after the aperture is mapped
([record](../decisions/2026-10-10-mga2164w-memory-walk.md)).
`VramMeasuredBytes=8388608` beside `VbeVramBytes=4194304`; DirectDraw's
heap at 800x600x32 is 8 MiB less the screen, and the engine and CPU both
pass the probe's cases at 7 MiB. Other families still believe 4F00h; no
other card has been seen under-reporting.
