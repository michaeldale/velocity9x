# DirectDraw fill and copy run on the HD 5450's DMA engine, pixel-correct through the HAL

Date: 2026-10-26

Status: **measured, one card, one boot of the final build.** M-BXMFA8's
HD 5450 (`1002:68F9`, Cedar), Phase 3 of
[ati-evergreen-cedar.md](../plans/ati-evergreen-cedar.md). Driver:
the `ati` package built from `f6e334b9` plus this commit's fixes
(`f6e334b9-dirty` in every capture). Captures:
[`../probe/m-bxmfa8-cedar-ddraw-2026-10-26/`](../probe/m-bxmfa8-cedar-ddraw-2026-10-26/).

## What runs

- The mini-VDD maps the 128 KiB register BAR and a 64 KiB ring window
  at BAR0 + 240 MiB (`V9XMINI_FN_EG_MAP`), after `runtime.asm` has read
  BAR0 and BAR2 with their high halves checked zero.
- `cedar_hw16.c` claims engine type `ATI_EVERGREEN` (11) for fill and
  copy when both windows map.
- `eng_eg.c` soft-resets the DMA block once per mode, starts a 32 KiB
  ring, and claims the engine only when a fence lands. Fills and copies
  are queued as one packet per row (`eg_dma.c`, host-tested).

## Deployment

The 0.30.1 `vbe` package was installed. Its three binaries
(`V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`) were replaced by the `ati`
build's through plain `dest=src` WININIT renames, then a `reset`. The
registry stayed the `vbe` INF's; the `ati` driver finds the chip by its
PCI scan. Every boot reached the desktop.

## Boots 4 to 6: the map refused, and why

Boot 4 came up `enable-ok` naming the HD 5450 but with
`EngineCaps=00000000`. Nothing said which step refused, so boot 5 added
`EvergreenEngine` / `EvergreenBar0` / `EvergreenBar2` to `V9XHW.INI`: the
BARs read `D0000000` and `FEAE0000`, the map refused. Boot 6 added the
mini-VDD's reason and value: reason 3, value `80000800`.

Those are not this service's answers. `V9XMINI_FN_EG_MAP` had been given
`0025h`, which `V9XMINI_FN_MTRR_WRITE`, defined near the top of
`V9XMAPI.INC`, already held, and the dispatcher tests MTRR_WRITE first.
Every map request reached the MTRR write with CX = 0 (pair 0), EBX =
`FEAE0000` and EDX = the caller's `FFFFFFFF`. It refused with its own
reason 3, "pair 0 already valid", and returned PHYSMASK0's low half,
`80000800`, which is what V9XBOOT.INI reads for pair 0
(`Mtrr0=00000006 80000800`). Per its contract, a refusal writes no MSR,
and pair 0 still reads the same.

`EG_MAP` is now `0026h`. `check-tree.ps1` now fails on any two mini-VDD
function numbers that are equal; it was shown to fail on the old number
and pass on the new.

## Boot 7: claimed

| | Value |
|---|---|
| `V9XHW.INI` | `EvergreenEngine=claimed`, `EngineCaps=00000003` |
| V9XDDP, four runs | `Result=COMPLETE`; `BltFillPixelOk`, `SrcCopyPixelOk` and all four overlap cases (down, right, and both on a pitched surface) 1 every run |
| V9XMSW | 6 of 6 resolution cycles, 4 of 4 depth cycles, PASS |
| V9XDDP after the cycles | all six blit checks 1 |
| V9XGDI `/auto` at 1024x768x16 | PASS |
| V9XTRACE, twice | `EngineType=11`, idle and FIFO timeouts 0, resets 0, `BuildsAgree=1` |
| Registers after the runs (V9XEGMM) | `DMA_RB_CNTL` `1B` (enabled, 2^13 dwords), `DMA_RB_BASE` `F0000` (GPU `0F000000`), `RPTR` = `WPTR` = `1AC0` |

The read and write pointers at 1AC0h bytes say 1712 dwords of packets
went through the ring and nothing was left in it. So the blits V9XDDP
checked were drawn by the engine, not the CPU path. `DMA_CNTL` reads
`28421000`: the context-empty interrupt bit the BIOS leaves set is
clear, as validation leaves it.

V9XDDP switches to 16 bpp for its own work (`AfterModeBpp=16`), so the
HAL was checked at 16 bpp only. 8 and 32 bpp rest on the builder's host
tests and the write probe's byte copies.

## Not established

- 8 and 32 bpp through the HAL.
- Throughput. `BltFillMs` read 0 or 1, too coarse to compare with
  anything.
- A DOS box with the ring running.
- GDI acceleration: the `ati` family's GDI path has no Evergreen arm.
- What the monitor shows. The owner has not answered the question asked
  on boot 3.
