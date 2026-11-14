# The HD 4600 asks for RP0 at engine open: 1150 MHz is granted at once and held under load

2026-11-14, i5-4570 (`8086:0412` rev 06, W9QI5A83). Follows
`2026-11-14-haswell-render-clock-400mhz.md`, which found the GT at the
BIOS's 400 MHz, idle or busy, against an RP0 of 1150. Evidence:
`docs\probe\hsw-4570-rp0-2026-11-14\`.

## Step 1: one request by hand (boot 262)

`V9XHSWMM.EXE /rpsreq:17` (DIOC 6 of `HSWMM.VXD`, new here): with the
driver's forcewake held, one write of `17000000h` to `RPNSWREQ` (`A008h`),
then `RPSTAT1` polled until its CAGF field equalled the request.

| | Before | After |
|---|---|---|
| `RPNSWREQ` | `08000000` (400 MHz) | `17000000` (1150 MHz) |
| `RPSTAT1` | `00040400` (400 MHz) | `00048B88` (1150 MHz) |

Granted after 106 reads (`Polls=6A`). Under 3DMark 2001 SE (full Pro
selection, 1024x768x32, compressed textures, as in the 400 MHz record),
two 120-sample windows about 10 s and 2 min in read 1150 MHz requested and
actual in every sample. The run completed: **3159** 3D marks. The same
selection at 400 MHz, earlier the same boot, scored 2357.

## Step 2: in the driver (boot 263)

The request moves into the driver, layered as `mtrr.c` is:

- `v9x_intel_hsw_rps_request` (`hsw_config.c`, host-tested): RPNSWREQ for
  RP0 from `RP_STATE_CAP`. A cap that reads 0 or all ones (an asleep or
  absent GT), has its fields out of order, or has an RP0 that RPSTAT1's
  7-bit field could not report is refused and nothing is asked for.
- `HSW_RPS` (`V9XMINI_FN_HSW_RPS`, 0030h, new): reads `RP_STATE_CAP`,
  `RPNSWREQ` and `RPSTAT1`; with a request, writes it and polls for the
  grant, bounded. Only with the engine open on a Haswell chip, so under
  forcewake. It does not choose the value.
- `hsw_hw16.c` asks for RP0 after every successful Haswell engine open and
  publishes `RenderClock`, `RpStateCap`, `Rpnswreq`, `Rpstat1` in
  `V9XHW.INI`.

Installed as the whole intel-gma set (`0e26f765-dirty`, this change) by
`run-release-checks.ps1 -Machine hd4600`: 8 of 8 files hash-matched after
the reset, all nine probe runs pass, no verdict changed from the 0.50.0
checks. `V9XHW.INI` on boot 263:

    RenderClock=rp0-granted
    RpStateCap=00040717
    Rpnswreq=17000000
    Rpstat1=00040B88

Under 3DMark 2001 SE on boot 263 (same selection), 120 samples about 10 s
in: 1150 MHz requested and actual in every one. The run completed: **3284**
3D marks. Its tree also differs from boot 262's in the HAL (main's committed
Gen3 and Haswell changes since 77495fa5), so it is recorded, not compared.

## Choices, and what argues against them

- **RP0, static.** No interrupt-driven turbo: the driver holds forcewake
  for the whole session, so the GT never sleeps, and a governor would only
  move the idle clock. The cost is that the idle desktop now also runs at
  1150 MHz, where it ran at 400. Power and temperature at idle were not
  measured. On a mobile Haswell (the family's 041E alias) that matters more
  than on this desktop; no mobile Haswell has run this.
- **Haswell only.** Ivy Bridge's request field is not at 31:24 (i915's
  `GEN6_FREQUENCY`), its CAGF is at 14:8, and its clock was read only at
  idle (350 MHz). The verb refuses Ivy Bridge.
- **Every open, reused or not.** Writing the same request again is
  harmless, and the "already at RP0" check did not fit: the 16-bit driver's
  `_TEXT` was 7 bytes over its 57,344-byte budget with it.
- New external symbols: the three `v9x_intel_hsw_rps_*` functions in
  HSWCODE (`check-tree.ps1` now counts ten far `hsw_config` calls) and the
  `V9xMiniHswRps` thunk.

## Not checked

- Thermal or package-power throttling over a long run: the longest window
  sampled was about 45 s, and the PCU may grant less once the CPU's power
  limit is reached.
- Idle power and temperature at 1150 MHz.
- What a DOS box or mode change does to the request: the engine closes and
  reopens around both, and the reopen asks again; not observed.
- Games other than 3DMark at the new clock.
