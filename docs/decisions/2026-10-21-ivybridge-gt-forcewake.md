# Ivy Bridge GT under forcewake on W9QI4E14: MT forcewake, RC6 enabled but never entered, rings idle

Date: 2026-10-21
Status: **measured twice (identical but for poll counts).** The only device
writes were the two forcewake request registers, in i915's order, and the
GT was left with the MT acknowledge set, as it was found. This is the GT
half of step 3 of `docs\plans\intel-ivybridge-gen7.md`. The display half
is `2026-10-21-ivybridge-read-only-mmio.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-mmio-2026-10-21\V9XIVMM-gt-run1.INI`
and `-gt-run2.INI` (`V9XIVMM.EXE /gt`), plus `V9XIVMM-run3-fixed.INI`, a
read-only run of the fixed build. The machine was on tier-0 `vbe` at
640x480x8 and stayed up: boot 5 throughout, desktop ready after each run.

## The handshake, and the acknowledge held at boot

The read-only run found `FORCEWAKE_MT_ACK` bit 0 already set at boot. On
this point the Haswell open's rule (refuse an acknowledge that is already
set) and i915 disagree. i915 does not refuse: at init it writes
`FORCEWAKE` = 0, reads ECOBUS, and clears every MT thread bit with a masked
write (`fw_domain_reset`, `WaRsClearFWBitsAtReset`). Every get then waits
for the acknowledge to clear, sets bit 0, waits for the acknowledge, and
waits for GT thread status C0 (`WaRsForcewakeWaitTC0`).

`IVMM.VXD`'s second call does exactly that, with interrupts off and each
wait bounded at 100,000 reads. It refuses if the acknowledge does not
clear. After reading, it puts (clears bit 0), then sets bit 0 again
because it was acknowledged on entry.

| Step | run 1 | run 2 |
|---|---|---|
| `130040h` on entry | `00000001` | `00000001` |
| `130090h` (legacy) on entry | `00000000` | `00000000` |
| ECOBUS before any wake | `84100020` | `84100020` |
| polls until the acknowledge cleared, after `A188h` = `FFFF0000` | 4 | 5 |
| polls until set, after `A188h` = `00010001` | 7 | 7 |
| polls until thread status bits 2:0 = 0 | 18 | 18 |
| polls until clear after the put | 6 | 5 |
| polls until set after the restore | 4 | 5 |
| `130040h` on exit | `00000001` | `00000001` |

So the acknowledge held at boot is released by i915's reset write within
a handful of reads. **For IVB the rule is i915's (reset, then wait for
clear), not the Haswell open's refusal.** Who set it, the BIOS or the
VBIOS, is not measured. It is set before Windows' driver loads.

**ECOBUS bit 5 (`FORCEWAKE_MT_ENABLE`) is set.** This part uses MT
forcewake, `A188h` acknowledged at `130040h`, and not the legacy
`A18Ch`/`130090h` pair. ECOBUS read the same before the wake as under it,
consistent with the GT being kept awake by the held acknowledge.

## GT registers, under forcewake

| Register | Value | Reading |
|---|---|---|
| `GEN6_RC_CONTROL` A090h | `88060000` | HW mode (31), EI mode 1 (27), RC6 (18) and RC6p (17) enabled: the BIOS turned RC6 on |
| `GEN6_RC_STATE` A094h | `00060000` | |
| `GT_CORE_STATUS` 138060h, bits 2:0 | 0 | RC0, awake |
| `GEN6_GT_GFX_RC6` 138108h | `00000000` | **no RC6 residency at all**: with the acknowledge held since boot, the GT has never entered RC6 |
| RCS TAIL/HEAD/START/CTL/ACTHD | all `00000000` | render ring never set up |
| BCS TAIL/HEAD/START/CTL/ACTHD | all `00000000` | blitter ring never set up |
| `MI_MODE` / `INSTPM` / `GFX_MODE` | `00000200` / `00008000` / `00002800` | `GFX_MODE` matches the 4570's `2800` |
| `GTFIFOCTL` 120008h | `0000003F` | 63 free entries; `GTFIFODBG` 0 |
| `GEN6_RPSTAT1` / `GEN6_RP_CONTROL` | `00040700` / `00000592` | |
| `GAM_ECOCHK` 4090h | `00000000` | |
| `GEN7_COMMON_SLICE_CHICKEN1` 7010h | `00000000` | bit 10 (RHWO disable) clear, which is what i915's IVB workaround leaves |
| `GEN7_L3CNTLREG1` B01Ch | `8C47FF80` | i915 writes `3C47FF8C` (`WaApplyL3ControlAndL3ChickenMode`) |
| `GEN7_L3CNTLREG2` / `3` | `00080040` / `00000000` | |
| `GEN7_L3_CHICKEN_MODE` B030h | `60000000` | i915 writes `20000000` |
| `GEN7_L3SQCREG4` B034h | `08000000` | bit 27 (`L3SQ_URB_READ_CAM_MATCH_DISABLE`) set; i915 clears it (`WaForceL3Serialization`) |

The rows marked i915 are the IVB GT workarounds of `intel_workarounds.c`
`ivb_gt_workarounds_init`. This BIOS leaves three of the four at other
values. The Haswell path writes none of them, and whether 3D on IVB needs
them is unmeasured.

## A bug, found and fixed in the same session

The first two `/gt` runs reported every pipe and port off. Their `[Mmio]`
held a single `R000000`, and their GTT was shifted by 144 entries (entry
6144 = `DD170001`). The cause was in the tool, not the hardware. The GT
call's data had been declared between the display result's header and its
arrays, and the display call copies that region as one block. 144 dwords
is exactly the GT block (30 offsets, a 24-dword header, three 30-dword
arrays).

The data was moved after the display result. `V9XIVMM-run3-fixed.INI` is
again identical to the read-only runs `run1`/`run2`, and the GT values did
not change with the fix. The two bad captures were discarded. The GT
halves of those runs agreed with the kept ones.

## What this settles for the plan

- **Forcewake on IVB:** `A188h`/`130040h`, MT enabled, i915's
  reset-then-wait. The Haswell constant `130044h` reads 0 here and would
  time out.
- **RC6:** enabled by the BIOS, and held off by the BIOS's own
  acknowledge. Releasing that acknowledge is a choice the driver will make
  (or not), and what happens when the GT then enters RC6 is unmeasured.
- **Rings:** both empty and off. Nothing has to be torn down before
  Velocity9x programs them.

## Not measured

- The GT after a release that is not immediately restored, and RC6 entry.
- Any ring command, blit or 3D state.
