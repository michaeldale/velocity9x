# DirectDraw fill, copy and flips run on Mullins' SDMA0 through Carrizo's engine

Date: 2026-11-08

Status: **measured, one machine, one boot.** M-JJN1ZZ's Radeon R3E
(`1002:9854`), boot 14, the `ati` DRV, VXD and HAL from `8e80796e` over the
`vbe` install. Phases 4 and 5 of [the plan](../plans/ati-mullins-gfx7.md).
Reports in
[`../probe/m-jjn1zz-mullins-directdraw-on-sdma-2026-11-08/`](../probe/m-jjn1zz-mullins-directdraw-on-sdma-2026-11-08/).

## The change

`8e80796e`: a Mullins row (`src\chipsets\ati\cik\mullins_hw16.c`) hands
fill, copy and flips to Carrizo's SDMA engine (`engines\eng_vi.c`) with
`V9X_DD_ENGINE_CAP_VI_MULLINS`, which selects Mullins' microcode, CIK's
golden values and no doorbell write. The write probe
([record](2026-11-08-mullins-sdma-write-probe.md)) is what licensed each
of those.

## Boot 14

- Installed by WININIT rename (plain renames, no `NUL=`), each staged and
  installed file SHA-256 checked against the package; staged files gone
  and `WININIT.INI` consumed after the reset. The reset took two relay
  tries (over five minutes) to answer; earlier resets of this machine
  answered on the first.
- `V9XBOOT.INI`: `Stage=enable-ok`, `MtrrWrite=ok`. `V9XHW.INI`:
  `Adapter=AMD Radeon R3E (Mullins)`, `MullinsEngine=engine`, BARs
  `E0000000` and `F0C00000`, `EngineCaps=10000007`.
- **V9XDDP**, four runs. V9XDDP tests at 640x480x16 whatever the desktop
  is, so each run is also a mode change and an SDMA restart:

  | Run | Desktop before | Fill | Copy | Overlap (4 cases) |
  |---|---|---|---|---|
  | 1 | 1024x768x16 | pass | pass | pass |
  | 2 | 1024x768x32 (`V9XMSW /set`) | pass | pass | pass |
  | 3 | after `V9XMSW /cycle:10` (10 of 10) | **not run** | pass | pass |
  | 4 | the same | pass | pass | pass |

- **The engine did the work.** V9XTRACE: `EngineType=12`,
  `EngineResets=0`, `CountBltEngine` equal to `CountBlt` after every run
  (7, 14, 20). No `V9XVIDMA.TXT` (the timeout record).
- **Flips are taken by the controller.** `FlipTakenAtDone` 23, 46, 69 after
  runs 1-3, `FlipNotTakenAtDone` 0, `FlipDeclined` 0. Twenty flips took
  322-332 ms, a frame each. `FlipPixelOk` read 0, as on every chip whose
  flips are real ([Carrizo's record](2026-10-28-carrizo-page-flips.md)).
- `V9XGDI /auto` PASS at 1024x768x32.

## Open

Run 3's fill was never sent: V9XDDP asks `GetBltStatus(DDGBS_CANBLT)`
first, the HAL's non-blocking poll answered `DDERR_WASSTILLDRAWING`
(8876021Ch), and the probe counts that as a failed fill. The answer is
legal DirectDraw, and the engine was not stuck: run 4 passed with no
reset between. One of four runs here; Carrizo's records show 23 of 23
answering 0. Not explained. The poll reads the fence 16 times
(`V9X_VI_QUICK_READS`), which the slower CPU here (1.2 GHz) may cover in
less time than the engine takes to finish the previous submission.

## Not measured

- Direct3D: the CP is GFX7 and not driven, so Direct3D stays on the
  software rasterizer.
- A game, the screen by eye, and 8 bpp.
