# Sandy Bridge HD 3000 on M-NZHWBR: config, display, GTT and the Gen6 forcewake handshake

Date: 2026-11-19
Status: **measured once each, read-only plus the forcewake handshake.** The
only device writes were `FORCEWAKE` (`A18Ch`), in i915's Gen6 order. Steps
1 and 3 of `docs\plans\intel-sandybridge-gen6.md`.

Captures: `docs\probe\m-nzhwbr-snb-hd3000-audit-2026-11-19\`:
`V9XBOOT.INI`, `V9XHW.INI`, `V9XMODES.INI` (the vbe driver's own records),
`V9XHSW.INI` (`V9XHSW.EXE`, config space), `V9XIVMM.INI` (`V9XIVMM.EXE`)
and `V9XIVMM-GT.INI` (`V9XIVMM.EXE /gt`). The machine was on the released
0.56.2 `vbe` driver (593a7890) at 1024x768x32 and stayed up on boot 60
throughout.

## Machine

M-NZHWBR (computer W9QI54F0, remote help): Core i3-2105, HD 3000
`8086:0112` rev 09, SUBSYS `1462:2111`, host bridge `8086:0100`, PCH
`8086:1E02` (7-series), 2 GiB, Windows 98 SE, agent 0.19.0. The owner
replaced the i5-3570K (HD 4000) with this CPU on 2026-11-19; the board and
subsystem id are the HD 4000's, the same model as W9QI4E14's. Monitor: a
Dell on HDMI.

## Tier 0, already passing

The vbe boot record: `Stage=enable-ok`, VBE 3.0 rev 0100, 1023 x 64 KiB
reported, the LFB at `E0000000` (BAR2), 18 modes from 640x480x8 to
1920x1200x32, all packed pitch except `0103` (800x600x8) at 832 bytes, as
on the HD 4000. PAT write-combining took (`PatWrite=ok`). Nothing more to
do at tier 0.

## Config space (`V9XHSW.INI`)

The Haswell decode accepts it as it stands, `Status=0000 Refused=0000`:

| Field | Value |
|---|---|
| GGC | `0211`: GMS 2 = 64 MiB stolen, GGMS 2 = 2 MiB GTT, locked |
| BDSM / BGSM | `DBA00000` / `DB800000` |
| BAR0 (GTTMMADR) | `F7800000`, GTT at +2 MiB |
| BAR2 (GMADR) | `E0000000`, MSAC 2 = 256 MiB |
| TOLUD | `DFA00000` |
| ASLS | `D9412018` |

Every one of these equals W9QI4E14's HD 4000 except ASLS, which moves
between reads there too.

## Display (`V9XIVMM.INI`, no forcewake)

The probe was extended for this run: `IVMM.VXD` accepts the seven Sandy
Bridge ids from i915_pciids.h, and the publisher leaves out pipe C's
watermark, FDI transmitter, PIPECONF and plane, and both MT forcewake
acknowledges, none of which exist on a Gen6 CPU. 49 offsets accepted.

- Pipe A, plane A and PCH transcoder A are on; B and C are off. The port is
  HDMI B (`E1140h` = `80000804`); CRT, LVDS and DP are off.
- Pipe A runs 1920x1200 (HTOTAL `081F077F`, VTOTAL `04D204AF`) with
  PIPE_SRCSZ `03FF02FF`, 1024x768, and panel fitter 0 on
  (`PF_CTL_0` = `80800000`, window 1920x1200 at 0,0). The VBIOS scales the
  mode to the panel rather than setting the panel's own timing.
- Plane A: `PRI_CTL_A` = `98000000` (enabled, 32-bit BGRX), STRIDE 4096,
  SURF 0, LINOFF 0, TILEOFF 0. The desktop is at aperture 0, linear.
- `130090h` (`FORCEWAKE_ACK`) reads 0: nothing holds forcewake at boot.
  The HD 4000 had its MT acknowledge set at boot.

## GTT

The BIOS maps stolen memory linearly from entry 0: 16383 entries map
`BDSM + n * 4 KiB`, all valid, cacheability `00b`, as on the HD 4000. The
last entry of the 64 MiB, 3FFFh, reads `AD4791A5` and does not. The
Haswell open's check (entries 6144-6175 against `BDSM + 1800000h` under
mask `FFFFF7F1h`) matches 32 of 32. 0x42419 entries are valid across the
whole 2 MiB table.

## Forcewake (`V9XIVMM-GT.INI`)

i915 6.6 initialises Gen6 with `FORCEWAKE` (`A18Ch`) as the request and
`FORCEWAKE_ACK` (`130090h`) as the acknowledge, through the same
`fw_domain_reset` and `fw_domains_get_with_thread_status` Ivy Bridge uses,
and never touches `FORCEWAKE_MT` there. `IVMM.VXD`'s second call now does
that on a Sandy Bridge id: `A18Ch` = `FFFF0000`, wait for the acknowledge
to clear, `A18Ch` = `00010001`, wait for it, wait for thread status C0,
read, put, restore only if it was held on entry. No ECOBUS read before the
wake.

| Step | Reads |
|---|---|
| `130090h` on entry | `00000000` |
| until clear after the reset | 1 |
| until set after `00010001` | 2195 |
| until thread status bits 2:0 = 0 | 19 |
| until clear after the put | 2 |
| `130090h` on exit | `00000000` |

The set takes 2195 reads where the HD 4000 took 7: here the GT was asleep.

| Register | Value | Reading |
|---|---|---|
| ECOBUS `A180h` | `85100000` | bit 5 clear: no MT forcewake, as i915 assumes for Gen6 |
| `GEN6_RC_CONTROL` `A090h` | `88040000` | HW mode, RC6 enabled (no RC6p) |
| `GEN6_GT_GFX_RC6` `138108h` | `22A3C151` | **the GT does enter RC6** at the desktop, unlike the HD 4000 |
| `GT_CORE_STATUS` bits 2:0 | 0 | awake under forcewake |
| RCS and BCS TAIL/HEAD/START/CTL/ACTHD | all 0 | neither ring set up |
| `MI_MODE` (RCS and BCS) | `00000200` | |
| `GTFIFOCTL` `120008h` | `0000003F` | 63 free entries; i915 keeps 20 in reserve |
| `GTFIFODBG` `120000h` | 0 | no FIFO errors |
| `GEN6_RPSTAT1` `A01Ch` | `00041100` | |

## What this licenses

The Haswell path's open needs only its forcewake registers changed for
Gen6: the decode, BARs, stolen layout, GTT check and ring registers all
read as they do on the HD 4000. Chip 2 of `HSW_ENGINE_OPEN` takes i915's
Gen6 pair with Ivy Bridge's reset and thread wait.

## Not measured

- Whether the BCS ring runs and blits land (the next step).
- `GEN6_STOLEN_RESERVED` (`1082C0h`): not in either capture; the driver's
  layout reads it at Enable and V9XHW.INI will show it.
- Gen6 RPS: `RP_STATE_CAP` and whether a `RPNSWREQ` request is granted.
- The GT FIFO: i915 waits for free entries before every GT write on Gen6.
  The Haswell path does not; with forcewake held and few writes this is
  not expected to matter, and it is unmeasured.
