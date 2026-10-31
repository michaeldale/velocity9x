# Tier-0 `vbe` on the HD 7470 (Caicos, 1002:6778) and HD 5770 (Juniper, 1002:68B8)

Date: 2026-10-31
Status: **Phase 0 done** for both cards
([plan](../plans/ati-evergreen-juniper-caicos.md)). On Caicos, every GDI,
mode and DirectDraw test passes. On Juniper, enable-ok was read; no tests
were run. Neither owner has said yet what the monitor shows.
Captures: [`docs/probe/m-d7tfw4-caicos-vbe-2026-10-31/`](../probe/m-d7tfw4-caicos-vbe-2026-10-31/),
[`docs/probe/m-bxmfa8-juniper-survey-2026-10-31/`](../probe/m-bxmfa8-juniper-survey-2026-10-31/).

## Installed by the owner, not by us

Both machines already had the signed 0.33.0 `vbe` package on the display
devnode before any command was sent (driver date 10-29-2026, the package
unpacked in `C:\extras\Velocity9x\velocity9x-0.33.0-vbe`). `V9XBOOT.INI`
and `V9XMODES.INI` carry `Build=0f45aa6f`, the 0.33.0 release commit. On
M-D7TFW4, `V9XDISP.DRV`, `V9XHAL.DLL`, `V9XMINI.VXD` and `V9XGL.DLL` in
`C:\WINDOWS\SYSTEM` match the release zip's copies by SHA-256. On M-BXMFA8
only `V9XDISP.DRV` was compared; it matches. Nothing was installed or
restarted for this record.

## The machines

| | M-D7TFW4 (W9QI6DFC) | M-BXMFA8 (W9QI3632) |
|---|---|---|
| Card | `PCI\VEN_1002&DEV_6778&SUBSYS_E145174B&REV_00` | `PCI\VEN_1002&DEV_68B8&SUBSYS_25431002&REV_00` |
| Chip | Caicos XT, HD 7470 | Juniper XT, HD 5770 (replaces the HD 5450 Cedar) |
| Monitor (PnP id) | `AOC2769`, EDID preferred 1920x1080 | `DELA07A`, EDID preferred 1920x1200 |
| CPU | AMD A10-5700 APU, 3.4 GHz (family 15h model 10h) | Core 2 Duo E7500, 2.93 GHz |
| RAM to Windows | 467 MiB | 2 GiB |
| Board | Acer (subsystem `1025:070A`), AMI BIOS 05/23/12 | Dell, as in the Cedar record |
| Other display | APU graphics `1002:9901`, "Standard PCI Graphics Adapter (VGA)", problem 22 (disabled) | none |
| Boot counter / agent | 3 / 0.13.1 | 48 / 0.13.1 |

The PCI ids were given from memory in the plan; both are now read from the
registry.

The disabled `9901` is the A10's Trinity graphics, Cayman-derived and
VLIW4. It is not part of this plan.

## What the driver reports

| | Caicos | Juniper |
|---|---|---|
| Stage | `enable-ok` | `enable-ok` |
| VBE | 3.0, OEM rev `0d0c`, `mem=256` (16 MiB) | 3.0, OEM rev `0c0d`, 16 MiB |
| LFB (BAR0) | `C0000000` | `D0000000` |
| `DpmiMap` | `01000000:ok` | `01000000:ok` |
| Modes listed / published | 53 / 36 | 53 / 39 |
| Desktop | 1024x768x16 | 1024x768x16 |
| MTRRs | 3 variable ranges: 0-512 MiB write-back, then uncached 8 MiB at 1E800000h and 16 MiB at 1F000000h | as on Cedar (`s=1`) |

On Caicos the 1600x1200, 1792x1344, 1856x1392 and 1920x1440 modes carry
attributes `00BA` ("supported" clear) and are not published, so 1400x1050
is the largest. The Cedar BIOS published 1600x1200 on the Dell monitor.
Whether Caicos's BIOS checks the AOC monitor before offering 1600x1200 was
not looked into. Neither BIOS offers its monitor's EDID-preferred mode, and
the driver says so (`Recommendation=... reason=edid-unpublished`).

The two uncached MTRR ranges on M-D7TFW4 end at 512 MiB. They sit at the
top of the memory Windows sees, where the A10's graphics carve-out would
be. They do not cover the LFB at `C0000000`, which therefore stays at the
default type, uncached. Not followed up.

## Tests on Caicos (M-D7TFW4), through the agent

| Test | Result |
|---|---|
| `V9XGDI /auto` at 1024x768x16 | PASS (twice) |
| `V9XMSW /set:1280x1024x32`, then `V9XGDI /auto` | PASS, PASS |
| `V9XMSW /cycle:10` | PASS, 10 of 10 |
| `V9XMSW /depth:10` | PASS |
| `V9XMSW /set:1024x768x16` (back to the owner's mode) | PASS |
| `V9XDDP` at 1024x768x16 | `Result=COMPLETE`; `V9XDD.INI` 60 `Ok=1` / 47 `Ok=0`, `V9XDD2.INI` 87 / 33 |

Every Z-buffer creation returns `0x88760231` (17 occurrences), as on
Cedar's tier-0 run: that is the software path's answer, not this card's.
The `Ok=0` set was not compared rung by rung with Cedar's 59 / 47. The
desktop screenshot afterwards is clean, at 1024x768x16.

`V9XGL.LOG` shows the owner ran the Quake 3 demo on 2026-10-29 with the
software engine (`cpu=400/17008` batches/triangles, no hardware paths).
3DMark 2000 is installed too.

## Not measured

- What either monitor shows. Screenshots read GDI back, not scanout.
- Juniper's GDI, mode and DirectDraw tests. The enable-ok and mode scan
  were the only goals there; the plan ports Caicos first.
- BAR2/BAR4, `CONFIG_MEMSIZE` and everything else in Phase 1. The
  register probe is next on Caicos.

## Next

Phase 1 on M-D7TFW4: V9XEGMM, read-only, including `MC_SEQ_SUP_CNTL`
(whether the BIOS left the memory controller's microcode running) and the
RCU backend fuses.
