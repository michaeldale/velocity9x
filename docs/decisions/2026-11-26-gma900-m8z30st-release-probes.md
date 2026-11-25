# GMA 900 on a second 915GM: the release probes pass on main, pixel shader 1.1 included; the texture pool, once the GTT capture stops reading its BAR3 as Gen4's, lets 3DMark 2000 finish

Date: 2026-11-26. Machine: remote help `M-8Z30ST`, a Lenovo 3000 C100
(the model of the 2026-10-07 survey the 915GM was claimed from), 915GM
GMA 900 `8086:2592` with its second function `8086:2792`, Windows 98 SE,
2 GiB RAM, desktop 1024x768x16. The owner offered it for testing on
2026-11-25 and had already installed released 0.60.0 (`b3996f07`).
Evidence:
[`../probe/gma900-m8z30st-2026-11-26/`](../probe/gma900-m8z30st-2026-11-26/).

The last GMA 900 run before this was 2026-10-20 on 0.21.1
([record](2026-10-20-gma900-tommo33-949fc64b.md)). The
[1.0 plan](../plans/release-1.0.0.md) lists the GMA 900 among the
remote-help cards that are Supported if they pass on the candidate.

## On 0.60.0, as found (boot 2)

`V9XBOOT.INI` `Stage=enable-ok`, VBE 3.0, `VbeVramBytes=8060928`, the
primary at a 2,112-byte pitch, MTRR write-combining set. `V9XHW.INI`
`Direct3D=hardware-gen3`, `EngineStatusEnable=map-refused` (the normal
first-enable reading, `intel_hw16.c`), and an engine stamp with a ring.
The ring starts at `0x6B0000` (`INTELMM.TXT`), as on the Acer in
October. Files in [`before/`](../probe/gma900-m8z30st-2026-11-26/before/).

## Release probes on main (boot 3)

`scripts/run-release-checks.ps1` installed the intel-gma package built
as `e53abdbd-dirty`, whose code is that of main `23e8926d` (the commit
added only records), by WININIT rename and `reset`. All eight files
hashed correct; 8 of 8 probe runs exited 0
([`release/`](../probe/gma900-m8z30st-2026-11-26/release/)).

| Check | Result |
|---|---|
| V9XD32 default (DDI 8), `/ddi6`, `/ddi5` | PASS each |
| V9XD32 T19 | PASS: pixel shader 1.1 (`FFFF0101`), every row 0 mismatches, Cube included; Dxt1/3/5 skipped, DXT not offered on Gen3 (`CheckDeviceFormat` 8876086A) |
| V9XD32 T18 | NO-TNL-DEVICE, as on every Gen3 part |
| V9XGLP | PASS (`LitResult=differ`, as five of six machines at 0.60.0) |
| V9XGLIDP, V9XFBCMP, V9XWPRS | PASS; presents 0.47 / 0.44 ms |
| V9XR3DP | COMPLETE, engine 3 |
| V9XSNA | no report (`abi-mismatch` on a snapshot reused from before the reset), as every machine at 0.60.0 |

This is the first run of the GMA 900 with DDI 8 and pixel shader 1.1.
The GMA 950's 0.60.0 checks run V9XD32 at DDI 6 and 5 only.

The first attempt installed nothing: the runner found both functions,
`8086:2592` and `8086:2792`, where the entry listed one. The entry now
lists both, as the GMA 950's does.

## 3DMark 2000 (Pro), default run

1024x768, 16-bit colour, 16-bit textures, Z16, triple buffer. After 17
flips and 991 DrawPrimitive calls 3DMark stopped with "3DMark could not
initialize your 3D Accelerator", `ValidateDevice : DDERR_OUTOFVIDEOMEMORY`
([`3dmark2000/3dmark-stuck.png`](../probe/gma900-m8z30st-2026-11-26/3dmark2000/3dmark-stuck.png)).
The dialog and 3DMark were closed afterwards.

Reading, not measured: the device was made (it drew and flipped), and
ValidateDevice reports on the textures bound, so the textures did not
fit in what the triple buffer and Z left of the 8 MiB stolen. The GMA
950 has a 32 MiB texture heap in system RAM since 2026-11-12
([Phase 3](2026-11-12-intel-gen3-texture-pool-phase3.md)). The GMA 900's
descriptor (`gma950_hw16.c`, `v9x_gma900_device`) has no pool entry, so
it has none; `STATUS.md` lists the GMA 900 as owed the pool.

Not tried: 3DMark with a double buffer, or at 800x600.

## The texture pool on the 915GM (boots 4 and 5)

Same design, same fix, decided by Michael the same day. Three gates
opened to `8086:2592`: `intel_pool16.c`, the mini-VDD's
`V9xMini_I9xx_Pool_Alloc` (on the 945's BAR3 path), and the GMA 900's
descriptor (the 945's 64 MiB GMADR mapping and `v9x_gma950_engine_pool`).

**Boot 4: refused.** `INTELGTT.TXT` `[IntelPool]` read `Result=
refused-alloc`, `Reason=2`, and 3DMark 2000 stopped as before
([`pool-refused-alloc/`](../probe/gma900-m8z30st-2026-11-26/pool-refused-alloc/)).
Reason 2 on the 945 path is a GTT taken to lie in BAR0. The GTT capture
decided that by address alone: a GTT at BAR0 + 512 KiB is the GM965's,
inside its 1 MiB BAR0. This BIOS places the 915GM's own BAR3 there
(`Bar0=D0000000`, `Bar3=D0080000`, here and in the 2026-10-07 survey of
the same model). So the 915GM has been read as Gen4 by the mini-VDD
since it was claimed, which also chose Gen4's offsets for the event
capture (`V9xMini_I9xx_Event_Table`).

Fix in `V9xMini_I9xx_Gtt_Capture`: an address at BAR0 + 512 KiB counts
as BAR0's only when the IGD's config 1Ch (BAR3 on Gen3) does not hold
it. On the GM965 and Ironlake 1Ch is the 64-bit GMADR's upper half,
zero, so their reading is unchanged by construction; **not run on
either.**

**Boot 5: mapped.** `[IntelPool]` `StateBefore=2`, `Result=kept` on the
later Enable, 32 MiB at aperture offset 8 MiB
([`pool/`](../probe/gma900-m8z30st-2026-11-26/pool/)). Release probes 8
of 8 exit 0, V9XD32 PASS at DDI 8, 6 and 5 (T19 PASS at DDI 8). 3DMark
2000 default, 1024x768x16, triple buffer: **1586**, run in 6.0 min,
recorded and not compared; 505,589 draws, none refused, no draw into
the presented page, no engine stall. The ten grabbed Helicopter frames
are whole and textured (`pool/grabs-sheet.png`).

## Not shown

- Any game, and 3DMark 2001 SE.
- The GM965 and Ironlake on the changed GTT capture.
- The event capture on the 915GM with Gen3's offsets.
- A full-screen DOS box with the pool mapped (owed on the GMA 950 too).
- The B10 Have Disk install on this machine (the owner's own install of
  0.60.0 worked; how it went was not seen).
