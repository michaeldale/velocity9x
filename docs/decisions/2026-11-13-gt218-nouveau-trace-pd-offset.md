# nouveau's GT218 channel, traced: the page directory is at + 200h, not + 1400h

Date: 2026-11-13

Steps 1 and 2 of [docs/plans/nvidia-gt218-linux-trace.md](../plans/nvidia-gt218-linux-trace.md).
The fix that follows from it is host-tested and **not yet run on a
card**.

## The trace

Michael ran `tools/diag/gt218-nouveau-mmiotrace.sh` as root on the
BringupKit target (GA-78LMT-USB3, Debian 13, kernel 6.12.107, GeForce
210 at 02:00.0, BAR0 `FB000000`, BAR1 `C0000000`, BAR3 `DE000000`), on a
boot with nouveau blacklisted. The script's last step failed (it changed
the tracer while its reader still held trace_pipe; fixed in c9699774),
but both markers are in the trace, so nouveau's whole load is covered:
25 s, 738,792 writes and 542,129 reads. nouveau initialised cleanly
under the tracer. The files stay on the target (`~/gt218-trace/`, 299 MB;
the load alone is `~/gt218-load.txt`); they are too large for the tree.

mmiotrace does see nouveau's BAR3 and BAR1 accesses, the plan's open
question: instance memory writes through BAR3 are in the trace with
their values.

**nouveau never kicks its channel during load.** It maps channel 1's
USERD (`FBC02000`, 2000h) and no access to it follows, in the load or in
the two minutes after. So the trace holds a complete channel setup and
no working kick.

## What nouveau writes for channel 1

Register order, BAR0: 250Ch, 2044h, 2100h, 2140h; an empty runlist
(32F4h, 32ECh = 0); 3200h, 3250h, 2500h; then for the channel 2604h =
`003FBD32`, read back, 2604h = `803FBD32`, 32F4h, 32ECh = 1. This is
the probe's order. Every instance-memory write batch ends with the BAR
flush (70000h), and every page table change with 100C80h = `00060001`.
Its init also clears bit 2 of 100C00h (`FC84418F` to `FC84418B`), which
the probe does not do.

The instance block, by offset (BAR3 writes; RAMFC + 98h gives its
address):

| Offset | Contents |
| --- | --- |
| 00C0h | an engine pointer: `00190000`, limit, start, start (a 4 KiB context; not PGRAPH's, which was never bound in the trace) |
| **0200h** | **the page directory**: PDE 0 `3FBE0003`, every other entry's high word `DEADCAFE` (nouveau's invalid marker) |
| 4200h | RAMFC: 3Ch `403F6078`, 44h `01003FFF`, 48h `00000FFE`, 50h `00011000`, 54h `000A0000`, 60h `7FFFFFFF`, 78h 0, 7Ch `30000001`, 80h `1C000540`, 88h cache >> 10, 98h instance >> 12 |
| 4300h, 4320h, 4340h | DMA objects (`7FC0003D` twice, `0019003D`), hashed into RAMHT |
| 4400h | PFIFO's cache area (from RAMFC + 88h) |
| 5400h | RAMHT (from RAMFC + 80h; entries seen at 5810h, 5818h, 5830h) |
| FFE0h | the push DMA object: `7FC00002`, limit `FFFFFFFF`, `FF000000` |

## The finding

G84 and later read the page directory at **instance + 200h**. The saved
nouveau sources held only `mmu/nv50.c`, whose `pd_offset` is 1400h;
`mmu/g84.c` was never fetched, and `bar/g84.c`'s `pgd_addr` 200h was
read on 2026-11-12 without the connection being made.

`nv50_chan.c` put the page directory at + 1400h and the VM DMA object
at + 200h. So the chip read the DMA object as its page directory. That
accounts for the runs on M-BXMFA8:

- PDE 0 was the object's flags, `7FC0003D`: bit 0 set (present), bits
  3:2 = 3, which `nv50_vmm_pde` gives as a page table in host memory, at
  a garbage address. Every translation in the first 512 MiB walked a page
  table in host memory nothing decodes. This is the master abort of runs
  14-23, and the reason the push address did not matter (run 23).
- PDE 1 was the object's third and fourth words, both 0: not present. An
  IB at 30000000h faulted cleanly, at that address, on this instance
  (run 22).
- With flags `7FC00002` (run 24) bits 1:0 are 2, neither encoding
  `nv50_vmm_pde` writes, and the master abort went. What the chip does
  with it is not known; the card still died within 50 ms (run 25).

Runs 15-21 changed things that were not the fault. The record of those
runs stands as measured; its readings of them are superseded here.

## The fix, and what is not yet known

`include/velocity9x/nv50_chan.h` now follows nouveau's measured layout:
page directory + 200h, RAMFC + 4200h, cache + 4400h, RAMHT + 5400h, our
two objects above it at + E000h and + E020h. `tests/host/test_nv50_chan.c`
asserts the page directory offset at compile time; it failed against the
old header and passes now. The probe takes the layout from the header.

## A second trace, with the channel in use

Same target and card, a fresh boot with nouveau blacklisted, the script
at 8ef86e57: it loads nouveau and then runs 30 frames of `kmscube`
through Mesa 25.0.7's nouveau driver (renderer `NVA8`, 29 frames
rendered, no nouveau error) between two trace markers. Copies of the
reduced files are in `build/driver-results/bringupkit-gt218-20261113/`.
The captured `lspci -vvv` also shows the root port `00:02.0` has no AER
capability.

- **The kick is a plain write.** Mesa's channel is channel 2 (2608h =
  `003FA942`, then `803FA942`; runlist count 2). The kernel wrote IB PUT
  at USERD + 8Ch (BAR0 `C0408C`) 487 times, counting up from 1, and
  never read channel 2's USERD. The probe's kick is the same write.
- **Channel 2's instance block has channel 1's layout**: page directory
  + 200h, RAMFC + 4200h with the same words (7Ch `30000001` again),
  RAMHT + 5400h, push DMA object `7FC00002` at + FFE0h.
- **PGRAPH's engine pointer is at + 20h**, as the probe has it:
  `00190000`, limit, start, start, with a 4C000h-byte context. nouveau
  clears bit 31 of the channel's table entry before writing it and sets
  it again after.
- **Objects**: 502Dh is one word `0000502D`, and its RAMHT context is
  `00100437` (engine 1 << 20 | offset >> 4). This is the probe's
  encoding.
- **The context program and image are ours, word for word.** nouveau
  uploaded 124 program words to 400328h; `nv50_grctx.c`, built with gcc
  on the target for chipset A8 and 1540h `F3010001` (this card reads the
  same), gives the same 124. The image nouveau wrote has 945 non-zero
  words, and ours has the same 945 at the same offsets with the same
  values. Step 1 of the PGRAPH plan had no reference to compare with;
  this is it.
- nouveau's `nv50_gr_init` writes, in the trace's order, are the ones
  the probe's gr stage makes (40008Ch = 4, the trap resets, the
  interrupt registers, 400500h = `00010001`, the program upload, zcull).

## Run 27 on M-BXMFA8 (boot 106, 2026-11-14): the corrected layout survives the kick

The owner was told beforehand and switched the machine on. The probe at
2807fb60 (built as `dde43228-dirty`; the in-kick timeline of run 26 is
now behind `/timeline` and was off), `/push:empty /flush:none
/push:class02 /stop:fifo`. Log:
`build/driver-results/m-bxmfa8-gt218-boot84/run27-V9XN5CH.LOG` (from
line 5851).

- The region read back, PRAMIN passed, 2604h is `80080042` (RAMFC at
  + 4200h).
- After the kick: PCI status `00100007` (no master abort), 100C90h
  `000D0FFF` (no VM trap).
- All 50 samples over the next 1.5 s answered, with the same values.
  The BAR1 marker read back afterwards, configuration space was
  unchanged, and the probe ended `EMPTY-PUSH-SURVIVED`. **The machine
  kept running**, the first kick of a mapped IB and push that did not
  stop it in 27 runs.
- From the first reading after the kick, **PMC_INTR `00000100` and
  PFIFO_INTR `00001000`**: PFIFO's DMA pusher raised an error
  (nouveau's `nv04_fifo_intr` names bit 12 DMA_PUSHER) and it stayed
  latched. PGRAPH_INTR 0, PGRAPH status 0.

So the page directory offset was the fault behind runs 14-26, as the
trace said. What is left is smaller: the DMA pusher rejects something in
this empty push or in the channel's pusher state. The probe cannot say
what yet, because the registers that describe it (CACHE1's DMA state,
3220h-3244h and the IB ones at 3320h-3334h) were taken out of the VxD's
read list after run 13, when reads of them timed out. That was with the
broken page directory; whether they answer now is not known.

Other differences from the trace, left as they are
until the corrected layout has been tried:

- RAMFC + 7Ch is `30000001` in the trace; the probe writes `30000FFF`
  (the class default the source names).
- nouveau's push DMA object is class 02h; the probe's default is 3Dh
  (`/push:class02` selects 02h).
- 100C00h bit 2, cleared by nouveau's init.
- PGRAPH's engine pointer offset (20h) is from source only.
