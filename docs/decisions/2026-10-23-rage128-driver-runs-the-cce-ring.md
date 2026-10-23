# Rage 128: the driver submits through the bus-master ring

Date: 2026-10-23

Status: **measured on one card; working.** The card is A8U4I5's Rage Fury
Pro (`1002:5046`) on a 440BX, boots 540 to 543. This follows the probe
that found the ring's placement
([record](2026-10-23-rage128-cce-ring-agp-fetches-but-wrong-data.md)).
Evidence: the files named below, in
[`../probe/a8u4i5-rage128-cce-ring-2026-10-23/`](../probe/a8u4i5-rage128-cce-ring-2026-10-23/).

## What the driver does

- **Mini-VDD (`V9XMINI_FN_R128_AGP_RING`):** reserves and commits 17
  aperture pages, uncached, once per boot (`_GARTReserve`,
  `_GARTCommit`). That is a 64 KiB ring plus a page for the wrap mirror.
  It never releases them, because the card may hold their address. It
  interprets nothing.
- **16-bit `rage128_hw16.c`:** asks for the pages at Enable and publishes
  them in the engine descriptor: `ring_linear_base` and `ring_bytes`,
  with the bus address in `reserve_offset`.
- **`r128_ring.c` (host-tested):** derives the ring's register values.
  - `AGP_BASE` is the 32 MiB window that holds the ring.
  - `PM4_BUFFER_OFFSET` is the ring's distance into that window.
  - The size code, the free-space arithmetic, and `BUS_CNTL`/`AGP_CNTL`
    as the probe ran them.
- **HAL `r128_cce.c`:** uses the ring when all of these hold:
  - pages are published;
  - `AGP_COMMAND` has `AGP_EN` set;
  - the plan accepts the placement;
  - `IsBadWritePtr` says the pages are writable.

  Otherwise it keeps the PIO CCE. A batch is copied into the ring, and
  then `DL_WPTR` is written with `DL_DONE`. The drain waits for `DL_RPTR`
  to equal the write pointer before the idle checks it already made.

## The first build hung; the engine reset fixed it

The first build started the ring the way ATI's driver appears to. It did
no engine reset, and set the write pointer to whatever the read pointer
held. Boot 540 drew every Direct3D test in V9XD32 correctly (T1 to T8,
textures, blending, Z read-back: `V9XD32-b540hang.TXT`). The machine then
locked hard, with no ping, at T4's Lock, the first drain after a draw
into an offscreen surface. No engine-timeout record was written.
`V9XTRACE-b540hang.INI` holds an access violation whose event ring ends
at mode-set calls, so it cannot be tied to the hang.

The probe then copied that start-up (`/noreset`, boot 542). The CCE moved
`DL_RPTR` to 6, wrote nothing, and stayed busy (`PM4_STAT` `800100C0`).
That run came straight after a run that had reset, used the ring and
stopped cleanly. So a ring restarted from NONPM4 needs the reset every
time, not only once (`R128CCE-noreset_*.TXT`). The driver stops the CCE at
every Lock and flip, so every restart after the first was one of these.

The driver now does the probe's start every time the ring starts:

1. stop the microengine;
2. reset the engine (`r128_do_engine_reset`): flush the non-GUI pixel
   cache, force the MCLK GUI and 3D clocks, pulse `SOFT_RESET_GUI`,
   restore;
3. write the ring registers;
4. write both pointers to 0, and read `DL_RPTR` back to confirm the
   reset took.

Before stopping a stuck ring it resets as well, as every probe stall did.
After a reset the 2D engine rewrites its init list, because
`V9X_DD_ENGINE_STATUS_VALIDATED` is cleared. The 3D path sends its whole
state with every batch, so it needs nothing.

## Wrap

`/wrap` starts both pointers at `3FFC`, 4 dwords before the 64 KiB
ring's end. It tags the page behind the ring `5B`.

- Without the mirror, the CCE ran the pairs at `3FFC`, `3FFE` and `0`,
  then stopped at 2 (scratch `5A000001`), then at 4.
- With Linux's 32-dword mirror, the same.

The CCE wrapped and never executed the page behind the ring
(`R128CCE-wrap_*.TXT`). The driver keeps the mirror anyway. A packet that
straddles the end was not tested.

## Boot 543, the reset build

| Test | Result |
|---|---|
| V9XD32 `/bpp16` (`V9XD32-b543.TXT`) | PASS, T4 included |
| Registers after it (`R128CCE-b543after_dump.TXT`) | `AGP_BASE` `D8000000`, `PM4_BUFFER_OFFSET` `01C00000`, `BUS_CNTL` `5033A300`, `AGP_CNTL` `38`, `DL_WPTR` `3A`: the ring was used |
| V9XR3DP, V9XGLP, V9XGLIDP (`ring-b543-*.INI`) | COMPLETE, PASS, PASS |
| V9XGL3P | SESSION, its usual result |
| Half-Life Direct3D timedemo (`HL-qconsole-b543*.log`) | 28.8 fps cold, 41.8 warm |
| Half-Life OpenGL timedemo | 14.5 fps cold, 15.3 warm |
| V9XD32 after the Half-Life session | PASS |

No hang across the Half-Life runs, which wrap the ring many times and
start it at every flip. The scores are recorded as measured; they were
not compared or chased.

## 3DMark 2000, boot 543

The default benchmark (1024x768x16, Z16, triple buffered) ran to the
end with no hang: **1335 3DMarks** (`3dmark2000-score-b543-ring.png`).

Michael questioned one Game 1 frame with no helicopter in view
(`3dmark2000-heli-b543-distant.png`). A second run, captured through
Game 1, shows the helicopter close up (`3dmark2000-heli-b543-a.png`,
`-b.png`), with the blended rotor blur, the HUD overlays, fog, the
tanks and the turret all drawn. The first frame was a distant camera
shot.

The snapshot after the first run (`V9XSNA7-b543-3dm.INI`):
- every Rage 128 refusal counter (`M64Policy*`) is zero, so nothing was
  refused to the CPU;
- `DrawsIntoPresented` is 970952. The count accumulates from the boot,
  and that boot also ran Half-Life and V9XD32. Which run it comes from
  was not separated.

3DMark's display panel names the adapter "Velocity9x ATI Rage XL PCI".
The Direct3D device reports "Velocity9x Rage 128". The adapter string is
most likely the display key's description, left from the Rage XL's
install on this machine. That was not checked.

## Not established

- Any other chipset or card. The 32 MiB window base is measured only on
  this aperture (`D8000000`, 64 MiB). An aperture smaller than 32 MiB, or
  a reservation that lands across a 32 MiB boundary, is refused by the
  plan and falls back to PIO.
- Whether 2D state outside eng_r128.c's init list survives the reset
  (owed: [plan](../plans/ati-rage128.md), "Todo: a GUI soft reset").
- A Glide game on the ring build.
- A packet straddling the ring's end.
