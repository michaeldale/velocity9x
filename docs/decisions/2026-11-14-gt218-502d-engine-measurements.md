# GT218 502D measurements for the HAL engine (step 4.0)

Date: 2026-11-14

Step 4.0 of [docs/plans/nvidia-gt218-nv50-engine.md](../plans/nvidia-gt218-nv50-engine.md):
what the engine may assume, measured with the probe on M-BXMFA8 (GT218
A8, boot 106) before any HAL code depends on it. Items are added as they
are run. All runs: `V9XN5CH /flush:none /push:class02 /pusher`. Logs in
`build/driver-results/m-bxmfa8-gt218-boot84/`.

## 1. Copy, non-overlapping, 32 bpp: passes (run 33)

The probe at 5ce5e33e (built as `f1433f74-dirty`). After the fifo, gr and
fill stages passed as in run 32, IB entry 3 carried
`v9x_nv50_push_copy`: a 16x16 block at (0, 0) of the 128x72 X8R8G8B8
test surface, which straddles the filled rectangle's corner at (8, 4)
and so holds both pattern and colour, copied to (100, 50). Log:
`run33-V9XN5CH.LOG` (from line 9893).

- IB get 4 = put 4, DMA get = put = `080223AC`, pusher error 0.
- PGRAPH_INTR 0, trap 0, idle; PMC_INTR 0, PFIFO_INTR 0; no master
  abort; the BAR1 marker intact.
- Read back through BAR1, top byte excepted: **256 of 256 copied pixels
  hold what the source held, 8960 of 8960 others are unchanged.**

So the copy push is right as written for this case: the source run at
230h and 244h, safe overlap 1, **blit control 0** (point sample, centre
origin), and the twelve words from 8B0h in the order destination
rectangle, du/dx and dv/dy as (fraction 0, integer 1), source x and y as
(fraction 0, integer). The control value and the order were recalled from
nouveau's X driver, not in the saved sources; this run is their
evidence. A copy with centre origin lands exactly, with no half-pixel
shift.

## 2 and 3. Overlapping copies, and 32, 16 and 8 bpp: all pass (run 34)

The probe at 43c16bd3 (built as `4dd7a97b-dirty`). After run 33's
stages, a test per depth on a 128x72 surface of its own: a CPU gradient
(every pixel differs from its neighbours), an engine fill of 64x64 at
(8, 4), an overlapping copy down and right (32x32 from (16, 16) to
(24, 20)), and one up and left (40x30 from (60, 30) to (50, 24)), a kick
each. The same operations ran on a model in memory whose copy reads
the whole source first. Log: `run34-V9XN5CH.LOG` (from line 10550).

| Depth | 502D format | Fill colour | Pixels matching the model |
| --- | --- | --- | --- |
| 32 bpp | E6h, X8R8G8B8 | `00A5C3E1` | 9216 of 9216 |
| 16 bpp | E8h, R5G6B5 | `A5C3` | 9216 of 9216 |
| 8 bpp | F3h | `5D` | 9216 of 9216 |

Every one of the nine kicks: PGRAPH_INTR 0, trap 0, idle, pusher error
0. IB get = put = 13 at the end; the machine up.

So, for the engine:

- **An overlapping copy is correct in both directions** with safe
  overlap set, with the same push; the engine need not order or split
  it.
- **16 bpp R5G6B5 is exact**, the full 16 bits (the fill colour has bit
  15 and bit 5 set). The RIVA 128's bit-15 problem does not exist here.
- **8 bpp works with format F3h** for the surface and the solid colour;
  the value is written as given.
- The destination and source formats can be the same value in one push.

## 4. The IB ring wraps cleanly: 600 kicks (run 35)

The probe at 944bc1db (built as `f74835c1-dirty`), `/many` added. After
the four earlier entries, 600 kicks of one small fill, each a new IB
entry with PUT advanced modulo 512, 150 to a VxD run. Log:
`run35-V9XN5CH.LOG` (from line 11579).

- IB get after each 150: `9A`, `130`, `1C6`, then **`5C`**: 604 modulo
  512, so the ring wrapped and PFIFO followed it.
- No wait timed out in 600; PFIFO_INTR 0, PGRAPH_INTR 0, pusher error 0
  after every batch. The four batches took about 60 ms between them.
- The three depth tests then ran on the wrapped ring and passed again,
  9216 of 9216 each.

So the engine can treat the ring as 512 entries indexed modulo 512 with
nothing special at the wrap, and reuse one pushbuffer address for every
kick.

## 6. The push DMA object in class 3Dh: works (run 36)

The same probe without `/push:class02`, so the VM DMA object reads
`7FC0003D`, the builder's default, and is both the push object and the
2D destination and source. Log: `run36-V9XN5CH.LOG` (from line 12697).

Every stage passed as with class 02h: the semaphore push, the 502D bind,
the fill, the copy and the three depth tests (9216 of 9216 each); no
master abort, no trap, no interrupt.

So the class was never the fault. Run 24's change with 02h came from the
misplaced page directory, where that word was being read as PDE 0 (the
trace record says how). The builder's default stays 3Dh; the plan's
rule to default to 02h is dropped.

## 5. A VBE mode set leaves the channel intact (runs 37-39)

The probe at a64118a4 (built as `a6724726-dirty`), `/flush:none /pusher
/check`: it initialises nothing, reads 23 registers and 14 sampled
region words, and uses the channel as found only if they still describe
it. The channel was the one run 36 left up. The desktop is the
Velocity9x nvidia tier-0 driver at 1024x768x16, whose mode sets are VBE
calls. Mode changes by V9XMSW through the agent; its reports are saved
beside the logs.

| Run | Before it | Channel state | 32 bpp fill and two copies |
| --- | --- | --- | --- |
| 37 | nothing (baseline, 12 minutes after run 36) | held | 9216 of 9216 |
| 38 | `V9XMSW /cycle:4`: 4 of 4 resolution cycles, PASS | held | 9216 of 9216 |
| 39 | `V9XMSW /depth:4`: 4 of 4 depth cycles, PASS | held | 9216 of 9216 |

In all three: 2604h `80080042`, 32ECh 1, 32F4h `00008020`, 40032Ch
`80008000` (PGRAPH still holds this channel's context), 40008Ch 4,
400500h `00010001`, 1704h `40008100`, PFIFO_INTR and PGRAPH_INTR 0,
PMC_ENABLE `DFF3F113`, and 14 of 14 sampled words of the instance block,
page table, runlist, objects and context image as written. IB get moved
on by three per run (0Dh, 10h, 13h), the three kicks of the run before.

So, unlike the RIVA 128, **this card's VBIOS mode set does not disturb
PFIFO, PGRAPH or the channel's memory**, across four cycles of each
kind (how many mode sets a cycle makes was not counted), and the channel
outlives the process that built it. The engine
needs no rebuild after a mode set. A cheap guard stays worth having,
because what was not tested can still lose the state: a full-screen DOS
box, standby and resume, and a display-driver restart.

## 7. The bring-up list from nv50_init.c, and nothing else (runs 40-41)

Step 4.1. The probe at f46768cd (built as `7c3eae2c-dirty`) replays the
register bring-up from the host-tested list in `nv50_init.c` (328
operations for this card: 142 for PFIFO and the trap enable, 182 for
PGRAPH and the context program, 4 for the bind) in place of its own
copies.

**Run 40**, boot 106, `/flush:none /pusher`: every stage passed as
before (the semaphore push, the 502D bind, the fill, the copy, and
32, 16 and 8 bpp with 0 bad pixels each). The list is the sequence the
earlier runs ran.

**Run 41**, after a restart (boot 107, at 38 s on Windows' tick count),
`/lean /pusher`: the region written through BAR1 and the list replayed,
and nothing else. No PRAMIN check, no flush page (100C08h read 0), no
PCIe reads, no PMC_ENABLE = FFFFFFFFh, no BAR channel (1704h is 0 after
a boot, run 18) and no BAR flush. Log: `run41-V9XN5CH.LOG` (from line
15748).

- Trap.Before 0, the enable took; both runlist waits 0.
- The semaphore push released `FEED0001`; the 502D bind, fill and copy
  passed; 32, 16 and 8 bpp each had 0 bad pixels of 9216;
  `Report.Result COMPLETE`. No master abort.

So the engine needs exactly two things from a cold card: the channel's
structures in video memory and `nv50_init.c`'s list. The BAR channel,
the BAR flush, the flush page and the PMC_ENABLE write, which the probe
carried from runs 16 to 19, are not needed.

## Step 4.0 is complete

Every item the plan listed is measured. For the engine: fill and copy
at 32, 16 and 8 bpp; overlap as it is; one kick per operation with the
ring modulo 512; the default DMA class; and no rebuild on a mode set.
