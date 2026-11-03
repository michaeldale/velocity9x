# DirectDraw and flips on the HD 7660D's DMA engine

Date: 2026-11-03
Status: **Phases 2 and 3 done** on M-D7TFW4's HD 7660D (`1002:9901`,
Trinity), boot 18. The ati driver claims the chip: fill, copy and flips on
its first DMA engine and D1, no Direct3D.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-ati-2026-11-03/`](../probe/m-d7tfw4-trinity-ati-2026-11-03/).

## The change

**Identity** (Michael, 2026-11-03: a fourth row, and a new engine bit).
`v9x_evergreen_trinity_device` in `cedar_hw16.c`, a new external symbol
for the per-object map audit, shares the engine hook, the 128 MiB heap
map and the diagnostics. It claims `SOLID_FILL | SCREEN_COPY | FLIP` and
`V9X_DD_ENGINE_CAP_EG_TRINITY` (`04000000h`, `engine_abi.h`). It does not
claim `D3D`, so the CP stays halted and no Direct3D or OpenGL is offered.
The `ati` manifest has a `trinity` chip on the same object (`Direct3D =
'not-advertised'`, the BIOS's baseline modes from boot 17). The backend
registry and `v9x_ati_device_ids[]` list `9901`.

**32-bit BARs.** `V9xPciReadEgBars` (`runtime.asm`) read each BAR's high
half first and refused anything that was not a 64-bit memory BAR. It now
reads the low half, takes a 32-bit memory BAR (type `00b`) as it stands,
and reads and checks the high half only for type `10b`. Other types are
still refused. The mini-VDD's `EG_MAP` service needed nothing: BAR2
`FEF00000` is 128 KiB aligned, BAR0 `C0000000` is 256 MiB aligned, and
`CONFIG_MEMSIZE` 256 meets its floor. The 4 MiB engine window at
BAR0 + 240 MiB and the 128 MiB heap map lie inside the 256 MiB BAR0.

**`DMA_MODE`.** `eng_eg.c` writes `DMA_MODE` 1 as `r600_dma_resume` does.
With the Trinity bit it skips the write, as `cayman_dma_resume` does
([probe record](2026-11-03-trinity-dma-write-probe.md)). `SRBM_SOFT_RESET`
was already read-modify-written there, so bit 16, which the BIOS leaves
set, is kept.

`run-checks.ps1` passed.

## Install

Built from `2a0b01ad` plus this change. The vbe INF's registry was kept, as
on the HD 7470's first ati boot. `V9XDISP.DRV`, `V9XMINI.VXD`,
`V9XHAL.DLL` and `V9XGL.DLL` went in by plain `dest=src` WININIT renames
(no `NUL=`), then `reset`: boot 17 to 18, desktop ready. The staged files
and `WININIT.INI` are gone, and the four installed files match the build by
SHA-256.

The HAL's upload timed out twice over the relay with the default 10 s
response timeout, after `WININIT.INI` had already been written.
`WININIT.INI` was overwritten with an empty `[Rename]` until a third try,
with `-ConnectTimeoutSeconds 120`, landed and matched. A restart in between
would have put the new driver beside the old HAL. Next time, write
`WININIT.INI` only after every staged file's hash matches.

## Boot 18

| | Result |
|---|---|
| `V9XHW.INI` | `Adapter=AMD Radeon HD 7660D (Trinity)`, `EvergreenEngine=claimed`, BARs `C0000000` / `FEF00000`, `EngineCaps=04000007`, `Direct3D=not-advertised`, `Direct3DMode=none` |
| `V9XBOOT.INI` | `enable-ok`, `DpmiMap=08000000:ok` (the 128 MiB heap map), 1024x768x16 |
| V9XDDP at 1024x768x16, two runs | `Result=COMPLETE`; `BltFillPixelOk` 1, `SrcCopyPixelOk` 1. The `Ok` keys (6 / 9) are the HD 7470's fill-and-copy boot 4, key for key |
| V9XDDP from 1024x768x32 | `Result=COMPLETE`, fill and copy 1 (it works at 16 bpp, `AfterModeBpp=16`, as on Caicos and Cedar) |
| V9XTRACE after the two 16 bpp runs | `BuildsAgree=1`; flips handled 46, declined 0, `FlipTakenAtDone` 46, `FlipNotTakenAtDone` 0, `ScanoutUnresolved` 0; engine timeouts and resets 0 |
| V9XTRACE after everything | flips handled 69, taken at done 69, declined 0; timeouts 0 |
| V9XGDI `/auto` | PASS at 1024x768x32, 1920x1080x32 and 1024x768x16 |
| V9XMSW | `/set` 1024x768x32, `/cycle:10`, `/depth:10`, `/set` 1920x1080x32, `/set` 1024x768x16: all PASS |
| Registers after (V9XEGMM) | `DMA_RB_CNTL` `1B` (enabled, 2^13 dwords), `DMA_RB_BASE` `0F0F0000` (GPU `F_0F000000`), `RPTR` = `WPTR` = `40h`, `DMA_MODE` 0, `SRBM_SOFT_RESET` `00010000`, CP halted, D1 at `F_00000000` |
| `C:\V9XDIAG\V9XEGDMA.TXT` (timeout record) | absent |
| Desktop screenshot after | clean, 1024x768x16 |

V9XDDP's `V9XDD2.INI` was not rewritten: this report is short (no D3D
sections run) and never splits. The file on the guest is the vbe run's,
so it is not in the capture.

`FlipDoneFirstPoll` read 0 of 46 here, against Caicos's 42 of 46, and
`FlipStillDrawing` 7,940 against 3,625. So here the previous flip was
never done by V9XDDP's next one. Neither figure says whether a flip waits
for the vertical blank, and neither was looked into.

## Not established

- Whether flips land at the vertical blank, and what the monitor shows
  during them. The owner was not asked.
- Any DirectDraw game.
- **Quake 3 on this build.** On the HD 7470, Quake 3 would not start
  while the engine was claimed without Direct3D (no software GL then;
  [Caicos flips record](2026-10-31-caicos-page-flips.md)). The same is
  expected here and was not tried. The owner's machine stays on this build
  until Direct3D lands or the vbe files are renamed back from
  `C:\extras\Velocity9x\velocity9x-0.34.0-vbe`.
- Cedar, Caicos and Juniper on this build. Their BARs are 64-bit, which
  takes the same checks as before in a new order, and their engine caps
  lack the Trinity bit. Covered by reading, not by a machine.

## Next

Phase 4, the CP: `ARUBA_pfp/me.bin` at Cayman's 2176 dwords each,
probably `ARUBA_rlc.bin`, and the decision on how Trinity's 3D sits beside
Evergreen's (plan decision 1).
