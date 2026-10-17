# Haswell Phase B1: the blitter runs from our ring, and the blit pitch is bytes

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE on the `vbe`
family at 640x480x8. `V9XHSWMM.EXE /bcs` + `HSWMM.VXD`, designed and
approved in `docs\plans\intel-haswell-direct3d.md` Phase B1. Evidence:
`docs\probe\hsw-4570-bcs-b1-2026-10-17\`.

## Run 1: aperture offset 1F00000h, stopped at stage 2

The 18 GTT entries were valid and linear (`DF100001`..`DF111001`), stage 1
passed. Stage 2 wrote `5A5AA5A5h` through GMADR + `1F00000h` and read back
`E97FAA55h`: the CPU's write did not read back. The run stopped there by
design. No GPU register was written; forcewake was never requested.

That region is the top megabyte of stolen memory (BDSM `DD200000`, TOLUD
`DF200000`, the region at `DF100000`). Haswell is reported to reserve a
protected range at the top of stolen memory, which would explain it; the
size of any such range on this board and the register that reports it were
not read, so that explanation is a hypothesis. What is measured: the CPU
cannot use `1F00000h`-`1F01000h` through the aperture, and the placement
must avoid the top megabyte.

## Run 2: aperture offset 1800000h, all stages

Moved to 24 MB with approval; nothing else changed. Status `000080FF`:

| Stage | Result |
|---|---|
| 1 PTEs | entries 6144-6161 `DEA00001`..`DEA11001`, valid, linear from BDSM |
| 2 CPU path | `5A5AA5A5h` read back; ring 0, status 0, scratch guard |
| 3 forcewake | acknowledged after 492 polls |
| 4 ring | CTL 0 before; START reads `01800000`, CTL `00000001`, HEAD 0, TAIL 0 |
| 5 store | `C0DE0001` after 2 polls, HEAD `10h` |
| 6 fill + store | `C0DE0002` after 1 poll, HEAD `38h` (= TAIL, 14 dwords) |
| 7 record | ACTHD `38h`, EIR 0 |
| 8 teardown | CTL, START, HEAD, TAIL all read 0; forcewake released after 3 polls |

The scratch holds exactly 256 dwords of `11223344h` (16x16) at a 256-byte
row stride, and the other 16,128 dwords are still the guard `A5A5A5A5h`.

## Findings

- **BCS executes from a ring in stolen memory** placed by us, with
  MI_STORE_DATA_IMM to a GGTT address and XY_COLOR_BLT to a GGTT address,
  under forcewake, and tears down cleanly. The GTT addresses are aperture
  offsets, as the GTT's linear mapping implies.
- **The XY_COLOR_BLT pitch for a linear surface is in bytes.** Vol 2b calls
  the field "Destination Pitch in DWords", beside a note about tiled
  surfaces; with a field of 256 the rows landed 256 bytes apart, not 1024.
  `hsw_3d.c`'s builder passes the field through, and callers pass bytes.
- **The top megabyte of stolen memory is not CPU-writable** through the
  aperture on this machine. Any ring, status page or heap placement must
  stay below `1F00000h`.
- The display and the agent were unaffected by both runs.

## Next

B2: DirectDraw fills and copies on BCS in the driver, which needs a hw16
device for `8086:0412` in `intel-gma`, mini-VDD verbs for this layout
(forcewake, BCS ring open and close), and an engine-type entry. Its own
design first.
