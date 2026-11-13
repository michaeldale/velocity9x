# GT218 PGRAPH 2D: from a VBE desktop to a 502D fill

Date: 2026-11-11

Status: research done. Step 1 done on the host (2026-11-11): nouveau's
generator ported to `src/chipsets/nvidia/gt218/nv50_grctx.c` behind
`include/velocity9x/nv50_grctx.h`; for A8 with 1540h = `F3010001` it gives
a 124-word program and a 77,440-word (302.5 KiB) context image with 945
non-zero defaults. Not yet run on the card, and not yet in the HAL. Phase 3 of
[docs/plans/nvidia-gt218.md](nvidia-gt218.md); Michael chose PGRAPH 2D over
the copy engine first (2026-11-11). Every register, offset and encoding
below is Linux nouveau's or envytools', read from source, unmeasured on
the card unless a record says otherwise.

## Starting state (measured)

From the [register probe](../decisions/2026-11-11-gt218-register-probe.md),
the [BAR window record](../decisions/2026-11-11-gt218-bar-windows.md) and
the [BAR1 heap record](../decisions/2026-11-11-gt218-bar1-heap.md), on
M-BXMFA8:

- chipset A8 stepping B1, 512 MiB DDR2/3, one partition; PBUS 1540h =
  `F3010001` (the generator's unit mask);
- PMC_ENABLE `DFF3F113`: PFIFO, PGRAPH and the copy engine on;
- no BAR VM (1704h/1708h/170Ch zero); BAR1 decodes video memory linearly
  from 0 and holds writes across its 256 MiB, so **every structure below
  is written by the CPU through BAR1**, with no PRAMIN window moves;
- the PFIFO channel table (2600h-27FCh) holds garbage;
- the driver maps 128 MiB of BAR1 for the DirectDraw heap (boot 83).

## Sources

Saved in `build/external/nv50/` (gitignored): nouveau `nvkm` (gr/nv50.c,
gr/ctxnv50.c and ctxnv40.h, gr/g84.c, gr/gt215.c, fifo/nv50.c, fifo/g84.c,
fifo/g98.c, fifo/chan.c, mmu/vmmnv50.c, dma/usernv50.c, core/ramht.c,
device/base.c), nvif (chan506f.c, push.h), nvhw class headers (cl502d.h,
cl006c.h, cl826f.h), envytools rnndb `g80_2d.xml`, Mesa `nv50_2d.xml.h`.
Nouveau is MIT-licensed; code ported from it carries its notice, as the
Evergreen work carries LICENSE.radeon.

nouveau's chipset table picks for 0xA8 (device/base.c, `nva8_chipset`):
`gt215_gr`, `g98_fifo`, `g84_bar`, `g84_mmu`, `nv50_instmem`.

## What a fill needs (nouveau's sequence, reduced)

1. **PGRAPH init** (gr/nv50.c `nv50_gr_init`, which `gt215_gr` uses):
   context switching on (`40008Ch = 4`), trap resets, per-TP trap enables
   for each bit of 1540h, interrupt status/enables, the context program
   upload, zcull for A8 (`402CC0h = 0`, `402CA8h = 2`). The only hardware
   read is 1540h.
2. **The context program** (gr/ctxnv50.c): generated, not firmware. Pure
   arithmetic over (chipset, 1540h, memory type: GDDR5 or not), about
   3,100 of its 3,347 lines apply to A8. Two outputs: the program (under
   the 512-instruction limit, uploaded `400324h = 0` then each word to
   `400328h`) and the per-channel context image's non-zero defaults.
3. **One channel, G84 style** (fifo/g84.c, fifo/nv50.c), hand-placed in
   video memory: a 64 KiB instance block with the page directory at
   +200h (corrected 2026-11-13: 1400h is the original NV50's; see
   [the trace record](../decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md));
   a page table identity-mapping VRAM with 64 KiB pages; the
   PGRAPH context image and its engine pointer at +20h in the engine
   area; a VM DMA object for the pushbuffer and the 2D engine; the 502D
   object; RAMHT (12-bit hash, entry = handle, engine << 20 | offset >>
   4); the cache area; RAMFC words; the runlist (`32F4h`, `32ECh`) and the
   channel table entry (`2600h + chid * 4 = ramfc >> 8 | 80000000h`).
4. **Submission is IB mode** (class 826F): a ring of 8-byte entries
   (address low; address high | size dwords << 10), kicked by writing the
   ring index to USERD + 8Ch, USERD at BAR0 + C00000h + chid * 2000h.
   Pushbuffer headers are NV06C: count 28:18, subchannel 15:13, method
   12:2. GET is USERD + 88h (ring) and + 58h/5Ch (top-level get).
5. **The 2D fill** (cl502d.h, g80_2d.xml): SET_OBJECT on a subchannel,
   DMA_DST, DST_FORMAT/LINEAR/PITCH/WIDTH/HEIGHT/ADDRESS_HIGH/LOW,
   DRAW_SHAPE RECTANGLES, DRAW_COLOR_FORMAT, DRAW_COLOR, OPERATION, then
   the point methods.
6. **Waits**, bounded the way nouveau bounds them: runlist update
   (32ECh bit 8), TLB flush (100C80h bit 0, and gt215's PGRAPH-idle
   flush, gr/g84.c, which also reads 400700h/400380h-400388h), GET.
   Interrupts are polled, not taken: 400100h (PGRAPH), 2100h (PFIFO),
   100C90h (VM faults).

Where nouveau assumes what this driver lacks: its own BAR VM, its own
instmem heap and allocator, interrupts. The plan replaces each with a
fixed layout and polling.

## Steps

Each step ends in a gate or a measurement, recorded in docs/decisions/.

### Step 1: the context program generator, host-tested

Port gr/ctxnv50.c's generator for the A8 branch to
`src/chipsets/nvidia/gt218/` as pure C: inputs chipset, the 1540h mask
and the memory type; outputs the program words and the image defaults.
Host tests: deterministic, under 512 words, every branch target inside
the program, the image size nouveau's arithmetic gives for 1540h =
`F3010001`. No reference dump of nouveau's output for A8 exists here, so
the tests check structure, not bytes; that is the step's main risk.

Done 2026-11-11. The port keeps nouveau's body verbatim: a diff against
`build/external/nv50/nouveau/nvkm_engine_gr_ctxnv50.c` removes 42 lines
(the kernel includes, the label enum, nouveau's `nv50_grctx_fill` and
`nv50_grctx_init`, the GDDR5 test), and everything else it adds is the
shim (kernel types, `nvkm_rd32` answering only 1540h, a bounded
`nvkm_wo32`, ctxnv40.h's emitters with BUG_ON as an overflow flag), the
labels as #defines, and `v9x_nv50_grctx_build`. The file carries
nouveau's MIT notice; it is exempt from the tree's C style so the diff
stays auditable (its header says why). `tests/host/test_nv50_grctx.c`
passes: program ends in CP_END, 124 of 512 words, no unresolved or
out-of-range branch, both passes and repeated builds agree, a short image
buffer is refused, GDDR5 changes exactly one image word and no program
word, a second TP grows the image. The program and image are not compared
with anything nouveau produced; the probe in step 3 is the first check.
When it ships in the HAL, the package's NOTICES.TXT needs nouveau's
notice.

### Step 2: the channel layout, host-tested

Pure C that builds, into a buffer, everything step 3 writes through
BAR1: instance block, PD/PT, DMA objects, 502D object, RAMHT with the
hash, RAMFC, runlist, IB ring and a pushbuffer (NOP, SET_OBJECT, the
fill). Host tests against the encodings above, word by word.

Done 2026-11-11: `include/velocity9x/nv50_chan.h`,
`src/chipsets/nvidia/gt218/nv50_chan.c`, `tests/host/test_nv50_chan.c`.
One B0000h-byte region (instance block at +0, page table +10000h,
runlist +20000h, IB +21000h, push +22000h, context image +30000h), 64 KiB
aligned, inside the first 512 MiB. Choices that are ours, not nouveau's:
the fixed offsets inside the instance block, a VM DMA object limited to
the 512 MiB the page table maps, handles `BEEF0201h` (DMA) and
`BEEF502Dh` (2D), subchannel 3, the PGRAPH pointer's fourth word as
nouveau writes it (start low) rather than as its DMA objects do (start
high). Two encodings are recalled, not in the saved sources, and marked
so in the code: the rectangle's second corner exclusive, and REF_CNT at
PFIFO method 50h read back from USERD + 48h.

### Step 3: the probe, V9XNV5CH

A VxD + Win32 probe in the N5MM pattern, on a fresh boot, writing its
structures through BAR1 above the driver's 128 MiB heap (BAR1 + 192 MiB,
so nothing the desktop or DirectDraw owns is touched), then in sub-steps,
each stopped on the first bounded wait that expires with a register dump:

- 3a: PFIFO alone: reset, clear the channel table, one channel, a
  pushbuffer of NOPs; pass when GET reaches PUT.
- 3b: PGRAPH init and the context program; SET_OBJECT 502D; pass when
  GET reaches PUT with 400100h clear.
- 3c: one 64x64 fill at BAR1 + 194 MiB, read back through BAR1; pass when
  every pixel is the colour and the words around it are untouched.

A hang means the owner power-cycles M-BXMFA8; the probe writes its INI
before each sub-step.

**Status (2026-11-14): step 3 passes, 3a to 3c, on M-BXMFA8** (runs
27-32, boot 106): a push executes, the 502D object binds with no PGRAPH
interrupt, and a 64x64 fill reads back exact. Two layout faults were
behind runs 14-26, found from an mmiotrace of nouveau on a second GT218:
the page directory offset (200h, not 1400h) and the page table's block
entries. Record:
[2026-11-13-gt218-nouveau-trace-pd-offset.md](../decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md).
Next is step 4, planned in
[nvidia-gt218-nv50-engine.md](nvidia-gt218-nv50-engine.md). The history
below is kept as written.

**Status (2026-11-12): paused at 3a after 14 runs and 11 resets.**
[docs/decisions/2026-11-11-gt218-channel-probe-run1-hang.md](../decisions/2026-11-11-gt218-channel-probe-run1-hang.md)
records them. Changed from the design above, by measurement: the window
is BAR1 + 128 MiB (BAR1 + 192 MiB was never measured; PRAMIN confirms
+ 128 MiB is video memory 128 MiB); there is no USERD read (reads that
time out lose the card, so GET cannot be polled the way nouveau does);
PGRAPH is initialised before the channel is bound; 100C08h gets a flush
page; every step goes to a write-through log first. Everything up to and
including the channel bind survives, and the kick loads the RAMFC into
CACHE1, but the machine has stopped within a second of every kick. Run 15
(2026-11-12) kicked an empty push and the machine still stopped. PCI
status showed a master abort the card received right after the kick, and
BAR1 read FFFFFFFF a second later. Run 16 left the flush page unset and
got the same master abort, so the flush page is not the cause. Run 17
enabled the VM trap as nouveau does: the kick traps nothing, and the
master abort still comes. Run 18 added a BAR channel (1704h) and the G84
BAR flush; the flush completed at once and the kick still master-aborted.
Run 19 found that PMC_ENABLE cannot be raised past the VBIOS's
`DFF3F113`, so it is not a difference either. Run 20 committed the empty
runlist at init: no change. Every difference from nouveau's init is now
ruled out. Run 21 read the card's and the root port's PCIe error state.
There is no AER below 400h, and no address was recorded. Continued
locally on a GT210 under Linux nouveau:
[nvidia-gt218-linux-trace.md](nvidia-gt218-linux-trace.md).

### Step 4: DirectDraw fill and copy on the engine

An `NV50` engine type in the HAL (engines\eng_nv50.c), the structures
reserved at the top of the BAR1 mapping instead of the heap, rebuilt
after a mode set if the probe shows the VBE disturbs them. V9XDDP's
DirectDraw keys before and after.

### Step 5: GDI fill and copy on the engine

## Decisions owed

- Where the engine structures live once the HAL owns them: top of the
  128 MiB mapping (shrinks the heap by a few MiB), or above it in an
  unmapped part of BAR1 (needs a second mapping).
- Whether to keep the PGRAPH context image nouveau-exact or start from
  zero (nouveau's comment says a zero context is enough for 2D; the
  program is still needed for context switches).
