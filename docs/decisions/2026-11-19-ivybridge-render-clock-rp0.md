# Ivy Bridge asks for RP0 too: the HD 4000 runs 1150 MHz; 3DMark 2001 SE 1743 to 2555, 2000 3121 to 4169

Date: 2026-11-19. Machine: M-NZHWBR (W9QI54F0), i5-3570K, HD 4000
(`8086:0162`), remote help. Follows
[`2026-11-14-haswell-render-clock-rp0.md`](2026-11-14-haswell-render-clock-rp0.md),
which asked for RP0 on Haswell only and left Ivy Bridge at the BIOS's
request. Evidence:
[`../probe/hd4000-nzhwbr-ivb-rps-2026-11-19/`](../probe/hd4000-nzhwbr-ivb-rps-2026-11-19/).

## Before

On 0.56.0 (boot 53, [record](2026-11-19-hd4000-desktop-on-0.56.0.md))
the driver did not touch the clock on this chip. The only reading on
record is W9QI4E14's at idle in October: `RPSTAT1` 00040700, 350 MHz
(RPn). Nothing was read under load before the change; the 0.56.0 scores
below are the "before".

## Change

The same request at engine open, with Ivy Bridge's fields. i915 puts
Ivy Bridge's request at `RPNSWREQ` 31:25 (`GEN6_FREQUENCY`) and the actual
clock at `RPSTAT1` 14:8 (`GEN6_CAGF`), one bit above Haswell's 31:24 and
13:7; `RP_STATE_CAP` (145998h) has the same layout.

- `v9x_intel_hsw_rps_request` (`hsw_config.c`) takes the chip, HSW_ENGINE_OPEN's
  0 Haswell / 1 Ivy Bridge, and shifts RP0 by 24 or 25. Any other chip is
  refused. Host test first: an Ivy Bridge cap of 00070D17 gives 2E000000.
- `HSW_RPS` in the mini-VDD accepts Ivy Bridge and uses its request mask
  (01FFFFFFh), shift and CAGF field when polling for the grant.
- `hsw_hw16.c` asks on Ivy Bridge's open path as well as Haswell's.

`run-checks.ps1` passed (`f551a7dd-dirty`, this change).

## After (boot 54, `f551a7dd-dirty`)

Installed by `run-release-checks.ps1 -Machine hd4000`: 8 of 8 files
hash-matched after the reset; V9XD32 default, `/ddi6`, `/ddi5`, V9XGLP,
V9XGLIDP, V9XR3DP, V9XWPRS and V9XFBCMP all pass, as on 0.56.0.
`V9XHW.INI`:

    RenderClock=rp0-granted
    RpStateCap=00070D17      RP0 1150, RP1 650, RPn 350 MHz
    Rpnswreq=2E000000        1150 MHz requested
    Rpstat1=00041707         1150 MHz actual

`V9XHSWMM.EXE /rps` (read-only, 120 samples over about 30 s each), on the
desktop before 3DMark and twice during 3DMark 2001 SE (about 1.5 and
3 minutes in): `RPNSWREQ` 2E000000 and `RPSTAT1` 00041707 in all 360
samples. The tool decodes with Haswell's fields, so its summary lines say
2300 MHz; the raw words are what the record rests on.

| 3DMark 2001 SE, saved settings (game detail Low) | Score |
|---|---|
| 0.56.0, boot 53, clock left alone | 1743 |
| This change, boot 54, RP0 requested | **2555** |

Same machine, same saved settings, one run each. The 4.8-minute run
(5.8 before) completed without a hang.

| 3DMark 2000, default benchmark, 1024x768 16-bit, 16-bit Z, triple buffer | Score |
|---|---|
| 0.56.0, boot 53, clock left alone | 3121 |
| This change, boot 54, RP0 requested | **4169** |

Same settings shown in both score screens (`hd4000/3dmark2000/end.png`
here, `../hd4000-nzhwbr-ivb-2026-11-19/hd4000/3dmark-score.png` before);
one run each, 5.1 minutes, no hang.

## What argues against it

- The idle desktop now also runs at 1150 MHz, as on the HD 4600; power and
  temperature were not measured. On the mobile 0166 (M-ZJQ2DX) that
  matters more, and it has not run this.
- The longest window sampled is about 30 s; package-power throttling over
  a long run was not looked for.
