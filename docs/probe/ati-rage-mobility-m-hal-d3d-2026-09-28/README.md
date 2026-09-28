# Rage Mobility-M: first HAL fill and first hardware Direct3D

Gateway Solo 2150, Velocity9x bound (see
[`../../decisions/2026-09-28-mobility-first-velocity9x-bind.md`](../../decisions/2026-09-28-mobility-first-velocity9x-bind.md)),
`V9XHAL.DLL` from commit `46a2444` (register-offset and block 1 fixes),
installed by WININIT rename. The DRV and VxD are from the `dbd6200` install.
Boot 15.

## DirectDraw engine fill: PASS

`V9XDDF.EXE` (`tools/diag/ddraw_fill_stage_win32.c`) runs one step at a
time, flushing each stage to disk first:

- `/nofill` created DirectDraw and a 64x16 off-screen surface, and asked
  `GetBltStatus(CANBLT)`, which runs the engine's validate and a status
  read. After it, `BUS_CNTL` read `7B33A001`, up from `7333A001`: the HAL
  set `BUS_EXT_REG_EN`, matching the stock driver's value
  (`ATIMM-BEFORE-DDRAW.TXT`, `ATIMM-AFTER-VALIDATE.TXT`).
- The full run's `COLORFILL` 0x07E0, then Lock: all 1,024 pixels matched and
  `Result=PASS` (`V9XDDF-FILL.TXT`).
- The engine, not the CPU fallback, did the fill: `DST_OFF_PITCH` became
  `0202FFFE`. That is a 64-pixel pitch with base `17FFF0`, 16 bytes below
  the surface at `180000`, which is the Phase 2 origin-repair batch
  (`ATIMM-AFTER-FILL.TXT`).
- The HAL snapshot has engine type 4 and caps `0x10`, with zero FIFO
  timeouts, idle timeouts and resets (`V9XSNAP-AFTER-FILL.INI`).

## Direct3D: first hardware triangles, probe INCOMPLETE

`V9XDDP.EXE` found the Velocity9x Direct3D HAL (`D3DHalFound=1`) with the
Mach64 engine's three texture formats, created the device, and passed:
`D3DTrianglePixelOk` (`0xF800`), reverse winding, both halves, sub-pixel,
shape, depth fog, vertex-alpha blend, and vertex alpha curve.

Most of the failures are the policy boundary, as intended. The texture
tests use textures larger than 8x8, and specular Gouraud carries specular
RGB, so the engine refuses those draws. The Z-buffer and mixed-engine
sections were never reached: they still carry the probe's not-run
placeholder `0x88760231`. So Z is untested, not failed.

The run did not finish. During the texture-mip matrix, the agent became
intermittently unreachable while ping still answered. Then the laptop
restarted (boot 16), and the report stops at
`TexM_256_1555_gapped_mipnear` with `Result=INCOMPLETE`
(`V9XDD-INCOMPLETE.INI`). What restarted it, and why the matrix stalled,
are not established.

## Hard lock at the first depth fill, and the 2D-mode fix

Boots 17-19, 2026-09-29. `V9XDDP.EXE` gained a write-through step log,
`C:\V9XDIAG\V9XDDT.TXT`: every report key, plus 18 texture-matrix step
points, is on disk before the next step runs. `V9XDD.INI` is not
evidence of where a lock happened; Win98 caches profile writes, and it
trails by hundreds of keys.

- The next two runs hard-locked (physical power cycle). The step log
  shows the texture matrix complete, and ends at `ZDepthFillHr 00000000`:
  the lock came during or right after the first DirectDraw depth fill
  following Z-enabled Direct3D draws (`V9XDDT-LOCK-AT-ZFILL.TXT`).
- Hypotheses the evidence killed:
  - DirectDraw-only surface traffic. `V9XTXM.EXE`, the texture matrix's
    create/lock/fill/release sequence without Direct3D, ran four passes
    to `result-pass` (`V9XTXM.TXT`).
  - Z buffer at the top of VRAM. `V9XDDF.EXE /zaddr` puts it at offset
    `0x180000`-`0x182000` of 8 MiB (`V9XDDF-ZADDR.TXT`).
- The remaining difference from every passing Phase 2 fill: the HAL's
  2D fill ran with the previous 3D draw's `Z_CNTL`, `ALPHA_TST_CNTL` and
  `SCALE_3D_CNTL` still live. The diagnostic scenes restored all state;
  the HAL did not. X.Org zeroes these before 2D (`ATIMach64Sync`,
  `Mach64DoneComposite`). The HAL now emits that 3-register batch ahead of
  every engine fill.
- With the fix (HAL build `ati-d3d-mach64-20260929-d`), boot 19 ran the
  probe to `RestoreHr` and `Result=COMPLETE` without a lock
  (`V9XDDT-2D-MODE.TXT`, `V9XDD-2D-MODE.INI`). `ZDepthFillOk=1` (raw
  `ABCD`, corner `ABCD`), `D3DZCompareOk=1`, `D3DZWriteMaskOk=1`, the four
  overlap blits pass, and the earlier triangle passes hold.
- Still failing, by the policy boundary: every texture larger than 8x8,
  and specular. `Tgt_*` fails too; that it is the same boundary is not
  checked. The final `V9XDD.INI` still shows the Z keys at their `0`
  placeholders, and the step log's later values are the measurement.
- The private-Z and mixed-ordering sections are opt-in (`/zprivate`,
  `/mixed`), which is why the default run left their placeholders. Two
  more runs followed on the same boot 19, both complete, and neither
  locked:
  - `/mixed` (`V9XDDT-MIXED.TXT`): `MixedOk=1`, with ordering and
    colour and depth encoding all passing. The HAL's draw was `F800` at
    depth `8000`, the CPU store was `07E0`/`4000`, and the HAL reject
    and accept draws left `07E0`/`4000` and `001F`/`6000`. HAL, CPU
    and HAL agree on colour, depth and order.
  - `/zprivate` (`V9XDDT-ZPRIVATE.TXT`): `D3DZPCompareOk=1` and
    `D3DZPWriteMaskOk=1` on a hardware device (`D3DZPIsHardware=1`,
    16-bit render and Z depth).
- Three clean runs on one boot, after two locks in two earlier runs.

## Cold boot: one more lock, in the Z ladder

Boot 20 was a cold boot, with the same HAL (`ati-d3d-mach64-20260929-d`,
hash-checked against the boot 19 copy). The first default run
hard-locked. Ping and the agent's TCP port still answered, but the agent
did not reply. The step log (`V9XDDT-BOOT20-LOCK-IN-Z-LADDER.TXT`) got
past the depth fill (`ZDepthFillOk=1`) and ends at `D3DZDeviceHr
00000000`. The lock is therefore somewhere in the first Z ladder:

- the target clear by CPU `Lock`;
- then four rungs, each a Z-state set, a Z-tested draw and a CPU pixel
  read.

The ladder had no step points of its own.

The probe (`ddp-zsteps-20260929-a`) now logs `z state`, `begin`, `draw`,
`end`, `read` and `done` for every rung, plus `z clear-target`. Boot 21
ran six default runs back to back, all complete, all passing the
triangle, depth-fill, Z-compare and Z-write-mask checks
(`V9XDDT-BOOT21-ZSTEPS.TXT` is the sixth). No lock has yet happened with
the rung steps in place, so which call locks is not established.

The totals since the 2D-mode fix: ten runs over three boots, and one
lock. That lock came in the first run after a cold boot, within about
two minutes of the desktop. Boot 19 also started from a power-off, and
its first run passed. So "first run after power-on" is a hypothesis, not
a finding.

Boots 22-25 were warm reboots through the agent. Each boot ran one
default run, started 7-12 s after the desktop came up. All four runs
completed and passed. The totals are now 14 runs over seven boots, with
the single boot 20 lock. A warm reboot does not reproduce it. Whether a
power-off does was not tested again.

A HAL snapshot was attempted. `V9XTRACE.EXE` exited `-1`, and the
`V9XSNAP.INI` it left dates from an earlier boot, so there are no
counter readings for boot 21.
