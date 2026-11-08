# The vbe family write-combines through PAT when the BIOS's MTRRs refuse: FbWrite 66 to 11,357 MB/s on the i5-4570

Date: 2026-11-08

Status: implemented; measured on one machine (W9QI5A83, i5-4570, HD 4600
under the `vbe` package), one boot each way. Not yet run on an AMD CPU or
on either field machine that asked for it.

Field reports: [GitHub issue 25](https://github.com/michaeldale/velocity9x/issues/25)
(the proposal), [issue 17](https://github.com/michaeldale/velocity9x/issues/17)
(GTX 970, Sandy Bridge, `r=11`) and
[issue 15](https://github.com/michaeldale/velocity9x/issues/15)
(FirePro W9000, X99, `r=7`). Precedent and mechanism:
[Haswell PAT](2026-10-28-haswell-pat-write-combining.md).

## The change

- `vbe_hw16.c` sets `V9X_HW16_DEVICE.write_combine_fallback`. When the
  MTRR plan refuses, it reads IA32_PAT through PAT_INFO, asks
  `v9x_pat_plan_wc`, and covers the frame buffer mapping through
  PAT_COVER, writing `PatWrite=` into `V9XBOOT.INI`. It types the video
  memory the BIOS reported, clamped to the mapping and rounded down to
  pages, for the same reason the MTRR window does: this family does not
  know the card's BAR.
- `v9x_pat_plan_wc` now also answers `V9X_MTRR_DEFAULT_NOT_UC` (7). A
  BIOS that makes WB the default and carves the PCI hole out with UC
  pairs stops the MTRR plan at the default-type test, before the overlap
  test that would have said 11. PAT WC is WC over any MTRR type (SDM
  vol. 3, table 11-7), so the route is the same. MTRRs disabled (2) is
  still refused: with E clear everything is UC whatever PAT says. Host
  test first: the W9000 case failed, then passed.
- The mini-VDD's PAT data, PAT_INFO, PAT_COVER and the four PAT
  procedures were behind `IFDEF V9X_INTEL_MMIO_FINGERPRINT`, so in every
  other family PAT_INFO answered zero. The issue 25 record said "every
  family links them"; that was wrong. The guards are gone; the code
  touches only PAT state and the MTRR flags every family already has.
- `runtime.asm`'s `V9xMiniPatInfo` / `V9xMiniPatCover` were inside the
  Intel-only block. They now sit behind `V9X_PAT_ROUTE`, a runtime define
  only intel-gma and vbe set: in every family the ati driver's `_TEXT`
  went 115 bytes over its 57,344-byte budget.

## Measured

Build `45136383-dirty` (main plus this change), `V9XDISP.DRV`,
`V9XMINI.VXD` and `V9XHAL.DLL` from the `vbe` package swapped over the
installed intel-gma files by WININIT rename, all three SHA-256 checked
against the package. The display class key stayed intel-gma's. Desktop
800x600x16, drawn correctly (screenshot checked).

Boot 190, write-combining on:

    Aperture=m=none bar=0 pci=1 b=e0000000
    Mtrr=cpu=000f cap=00000d0a def=00000c00 n=8 r=11 s=0 b=00000000 z=00000000
    MtrrWrite=refused
    PatWrite=ok pages=00001000 hi=00070101

`VbeVramBytes=33488896`, so the 16 MiB mapping bounds the cover: 4,096
pages. `hi=00070101` is entry 4 WC; entry 5 was already WC from the BIOS,
as on the intel-gma boots. Boot 191: `C:\V9XDIAG\NOWC.TXT` in place,
`PatWrite=off-marker`, nothing else changed.

dispbench `a52ad8c` (tools rebuilt at that commit),
`run-bench.ps1 -Mode 1024x768x16 -Tools DBTIME,DBGDI,DBDD`, results in
dispbench `docs/results/w9qi5a83-hd4600-v9x-vbe/20261108-180708` (off)
and `20261108-180338` (on):

| MB/s | off (boot 191) | on (boot 190) |
|---|---:|---:|
| FbWrite | 66.0 | **11,357.0** |
| FbRead | 35.4 | 228.8 |
| FillRect 1024x768 | 65.6 | 11,318.8 |
| FillRect 64x64 | 65.3 | 3,875.0 |
| FillRect 16x16 | 58.8 | 341.5 |
| BitBlt bitmap to screen 256x256 | 65.4 | 4,725.0 |
| BitBlt DIB to screen 256x256 | 65.4 | 4,186.1 |
| BitBlt screen to screen 256x256 | 24.4 | 103.7 |
| Ellipse 128x128 | 59.2 | 851.7 |
| DirectDraw Blt system to video 512x512 | 132.0 | 16,094.1 |

The off figure matches the intel-gma package's uncached FbWrite on this
machine (66 MB/s, 2026-10-28); on is above intel-gma's PAT figure there
(9,238 MB/s), which was a different tool build.

FbRead also rose (35 to 229 MB/s). Not examined; WC reads are uncached
reads, so a change in the read loop between the two boots or a
side-effect of the type change is possible. Recorded, not explained.

On the on run DBGDI wrote all 134 measurement sections and died before
its `[Result]` section (dispbench reports `died`); the off run completed.
Not repeated. The GDI rows above come from the complete sections.

The machine was put back on its intel-gma files afterwards (boot 192,
hashes match the boot 189 copies, `PatWrite=ok pages=00004000` and
`PatReserve` as before). That intel-gma image predates this change; the
rebuilt one was booted later the same day (below).

## Other families and an MTRR-route machine (same day, after the commit)

Packages from the same `45136383-dirty` tree, DRV/VXD/HAL by WININIT
rename, SHA-256 checked, each machine put back afterwards and checked:

- **i5-4570, rebuilt intel-gma** (boot 193): `PatWrite=ok pages=00004000
  hi=00070101`, `PatReserve=ok pages=00000050`, and `V9XHW.INI`
  line-for-line the same as boot 189's. The `V9X_PAT_ROUTE` move did not
  change the HD 4600's route. Back on the boot 189 files at boot 194.
- **A8U4I5, Pentium III, Rage 128 Pro, rebuilt ati** (boot 683): the
  mini-VDD now carries the PAT code (15,476 bytes against 0.42.0's
  14,452) and loads; `MtrrWrite=ok s=3 b=dc000001 m=ff000800` as on
  0.42.0, no `PatWrite=` line (ati has no fallback), the Rage 128 engine
  up (`EngineStamp=type=0000000A`).
- **A8U4I5, vbe** (boot 684): `MtrrWrite=ok s=3`, no `PatWrite=` line,
  desktop drawn. Where the MTRR route works the fallback is not called:
  the third "Done when" point of issue 25, measured.

A8U4I5 back on the released 0.42.0 ati files (`59b93e48`) at boot 685.

## Not known

- Any AMD CPU: PAT has only been measured on this Haswell.
- Issue 15's `r=7` path on hardware: covered by the host test only.
- Whether the W9000 or the GTX 970 boot with `PatWrite=ok`.

Gates: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1` pass.
