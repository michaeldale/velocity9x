# HD 2600 XT DMA: a COPY's source and destination must start on 8 bytes

Date: 2026-11-13

Status: **measured, one card, boots 12-14.** M-NZHWBR's HD 2600 XT
(`1002:9588`, RV630). Tool: `V9XRVDMA.EXE /copyplan /takeover`
(`tools\diag\rv630_dma_win32.c`, built from this commit's tree), which
replays the copy that stopped 3DMark 2000
([issue](../issues/2026-11-13-rv630-dma-copy-stall-3dmark2000.md)) inside
the probe's 64 KiB window: rows of `/cpd:` dwords from a source at pitch
`/cpsp:` and offset `/cpso:`, to a destination at pitch `/cpdp:` and
offset `/cpdo:`, every row read back. Captures:
[`../probe/rv630-dma-copyplan-2026-11-13/`](../probe/rv630-dma-copyplan-2026-11-13/).

`/takeover` runs over the ring the `ati` driver enabled; each run was made
in a boot whose HAL had not used the engine since the mode set, after a
`reset`.

## Runs

Six rows of `D2h` dwords, source pitch `800h`, destination pitch `348h`
(3DMark's shape) unless named:

| Boot | Source offset | Destination offset | Dwords | Result |
|---|---|---|---|---|
| 11 (after the 3DMark stall) | any | | | nothing ran: the engine stayed busy (`DMA_STATUS 44809046`) through the probe's DMA soft reset |
| 12 | `260h` (32 B) | `DD0h` (16 B) | D2h | all six rows exact |
| 12 | `25Ch` (4 B only) | `DD0h` | D2h | **hang**: no row written, fence never lands, `DMA_STATUS 44809042` |
| 13 | `250h` (16 B) | `DD0h` | D2h | exact |
| 13 | `258h` (8 B) | `DD0h` | D2h | exact |
| 13 | `260h` | `DD8h` (8 B) | D2h | exact |
| 13 | `260h` | `DD4h` (4 B only) | D2h | fence lands, **no row in place**, one guard dword overwritten: written elsewhere |
| 14 | `258h` | `DD0h` | D1h (odd) | exact |
| 14 | `258h` | `DD0h` | 3 | exact |

## What it settles

- A COPY whose source is 4-byte but not 8-byte aligned hangs the DMA
  engine, and a DMA soft reset (SRBM_SOFT_RESET bit 12) does not recover
  it; a machine reset does. That is what 3DMark 2000's first copy did:
  its source rows started at `...5A5C`, `...625C`, `...6A5C`.
- A destination on 4 bytes only is written somewhere else. One guard just
  before the rows changed, which is what dropping the low three address
  bits would do; that reading is not proven.
- Dword counts need no rounding: D1h and 3 copied exactly.

So `r6_dma.c` now plans a fill or copy only when both ends of every row
start on 8 bytes, which takes the pitches too
(`V9X_R6_DMA_ADDRESS_ALIGN`); the rest goes to the CPU. The earlier DMA
write probe never met this: its buffers were 256-byte aligned and its
rectangle rows started on 16 bytes.

## Not measured

- Sources and destinations past the 64 KiB window, or in the visible
  screen.
- Whether a split into an aligned body and CPU-written edges would keep
  more rectangles on the engine; today the whole operation goes to the
  CPU.
