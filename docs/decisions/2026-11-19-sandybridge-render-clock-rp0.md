# Sandy Bridge HD 3000: RP0 asked for and granted, 1100 MHz

Date: 2026-11-19
Status: **measured once, M-NZHWBR boot 62.** Step 6 of
`docs\plans\intel-sandybridge-gen6.md`. Build `5fb08cda-dirty`.
Evidence: `docs\probe\m-nzhwbr-snb-hd3000-rps-2026-11-19\`.

## Why the Ivy Bridge fields

i915 6.6 programs RPS the same way on Sandy Bridge and Ivy Bridge: the
request is `GEN6_FREQUENCY(x)` = x << 25 in `RPNSWREQ` (`A008h`), and the
actual clock is `GEN6_CAGF`, bits 14:8 of `RPSTAT1` (`A01Ch`); only
Haswell and Broadwell take the `HSW_` fields (`gen6_rps_set`,
`intel_rps_get_cagf`). `RP_STATE_CAP` is `145998h` on both. So chip 2
takes Ivy Bridge's encoding in `v9x_intel_hsw_rps_request` (host test
first: it failed on the Sandy Bridge case, then passed) and in the
mini-VDD's `HSW_RPS` verb, and `hsw_hw16.c` asks at every open, as on
Ivy Bridge.

## Boot 62 (`boot62-V9XHW.INI`)

| Key | Value | Reading |
|---|---|---|
| `RpStateCap` | `00111116` | RP0 `16h` = 1100 MHz, RP1 `11h` = 850 MHz, RPn `11h` = 850 MHz |
| `Rpnswreq` | `2C000000` | `16h` << 25: RP0 asked for |
| `Rpstat1` | `00041611` | CAGF (14:8) `16h`: 1100 MHz running |
| `RenderClock` | `rp0-granted` | |

RPn equals RP1 on this part, so the cap reports no range below 850 MHz.
1100 MHz is Intel's rated maximum for the i3-2105's HD 3000. Before this
build nothing asked, and the read-only capture saw `RPSTAT1` =
`00041100` (CAGF `11h`, 850 MHz) with the GT awake under forcewake.

At RP0, `V9XDDF` passes at 1024x768x32 and `V9XDDP`'s 640x480x16 chain
takes 23 of 23 flips (`Flip20Ms` 332). No engine timeouts or resets.

## A note on V9XDDP's CPU blits

Each `V9XDDP` run adds 7 blits to `CountBlt` and 3 to `CountBltEngine`,
here and on boot 61 before RPS. The HD 4000's runs show the same 4 of 6
on the CPU (`w9qi4e14-ivb-hd4000-flips-2026-10-21\boot7-ddp-V9XSNA3.INI`):
the probe's own cases on this path, not a Sandy Bridge difference.

## Not measured

- Any effect on a workload: nothing 3D runs on the chip yet.
- Whether the PCU holds 1100 MHz under load or drops it for power.
