# GT218 channel probe, runs 1-26: BAR0 reads that time out lose the card; any kick stops the machine, with a master abort

Date: 2026-11-11

> **Corrected 2026-11-12 (boot 84), see "The report, read after the
> restart" below:** stage fifo never ran. The card had already left PCI
> configuration space when the fifo run loaded the VxD, and the region
> run's first accesses through the BAR1 window at + 192 MiB read
> FFFFFFFF. The TLB flush suspect below is dead.

Step 3 of [docs/plans/nvidia-gt218-pgraph-2d.md](../plans/nvidia-gt218-pgraph-2d.md).

## What ran

`V9XN5CH.EXE /stop:fifo` (`tools/diag/nv50_channel_probe*.{asm,c}`,
`scripts/build-nv50-channel-probe.ps1`), built from 6737d4f1 plus the
probe (uncommitted at the time), on M-BXMFA8 boot 83 (the BAR1-heap
`nvidia` driver, 1024x768x16 desktop). Its stages, each one DIOC run of
N5CH.VXD:

1. `pre`: reads only.
2. `units`, `region`: read 1540h, build the channel with nv50_chan.c, write
   the B0000h-byte region through BAR1 at video memory 192 MiB, fill the
   2D target, read four words back. INI flushed after.
3. `fifo`: in **one** run: PMC_ENABLE PFIFO off and on, nouveau's
   nv50_fifo_init constants, interrupt status and enables (PMC_INTR_EN not
   touched), the channel table cleared, cache1 and caches on, **a raw VM
   TLB flush for PGRAPH (`100C80h = 1`) and a bounded poll of it**, channel
   1 bound and enabled, runlist committed and polled, IB PUT = 1, IB GET
   polled, status reads.

## What happened

The exec never returned. The agent's last relay check-in was about 166 s
before the transport timed out, and the relay showed the machine offline
from then on: the machine stopped, not just the probe. The owner was asked
by remote-help chat to power-cycle it (the message is only delivered when
the agent next checks in, so it does not reach them while the machine is
hung).

Nothing has been read back yet. V9XN5CH.INI should hold the `pre` and
`region` stages (flushed before `fifo` began); it is to be fetched after
the restart.

## What the evidence allows

The hang is somewhere in the single `fifo` run, which is all the run can
say. Every one of its MMIO waits is bounded by a loop count, so a loop
cannot have spun for ever; a hang that stops the whole machine is a bus
access that never completes (a stalled BAR0 read), or a GPU state that
stalls the bus.

The leading suspect, not confirmed: the raw PGRAPH TLB flush. nouveau
never flushes for PFIFO, and for PGRAPH on the GT215 family it uses
`g84_gr_tlb_flush` (gr/g84.c), which first clears PGRAPH's FIFO access
(400500h bit 0) and waits for its units to idle before writing 100C80h,
with the comment "unfortunate hw bug workaround". This run flushed PGRAPH
raw, before PGRAPH had been initialised. Other candidates in the same run:
the PFIFO reset itself, enabling caches with a stale state, and the USERD
IB GET poll on a channel PFIFO could not load.

## Changed for run 2

- The PGRAPH TLB flush is removed from stage fifo (the VM is new; nothing
  has cached it).
- Stage fifo is three DIOC runs (`fifo-init`, `fifo-bind`, `fifo-kick`),
  stage gr two (`gr-init`, `gr`), and every run first flushes
  `Report/Progress=<run>` to the INI, so the next hang names its run.

## The report, read after the restart (boot 84)

`V9XN5CH.INI`, kept as `build/driver-results/m-bxmfa8-gt218-boot84/
V9XN5CH-run1.INI`:

```
pre.Status=00000017      units.Status=00000017   region.Status=00000017
fifo.Status=00000000     fifo.RefusedAt=0        Result=FIFO-REFUSED
[Pre]   PmcEnable=DFF3F113  PgraphCtxCtl=00000000  PgraphTrapClass=00000000
        IbGet/IbPut/Ref (USERD C02088h...) = FFFFFFFF
        PgraphIntr..PgraphTrapData (400100h-400810h) = FFFFFFFF
        VmFault100C94=0C18050E  100C98=00F00002  100C9C=00000200
[Region] Units=F3010001  ReadPde0, ReadRamfc80, ReadPteSelf, ReadDma = FFFFFFFF
```

- `fifo.Status=0` has no FOUND bit and no REFUSED bit. In N5CH.VXD that is
  `NC_Find` failing: the configuration scan for 10DE:0A65 found nothing
  when the fifo run loaded the VxD. **No fifo op ran.** The EXE then
  wrote its report and exited normally; the machine stopped after that.
- The card was alive for `units` (1540h read F3010001). Every read in
  `region`, the probe's first access through its BAR1 window (BAR1 +
  192 MiB, video memory 192 MiB), returned FFFFFFFF.
- In `pre`, the USERD reads and PGRAPH 400100h-400810h read FFFFFFFF
  while 40008Ch, 400814h and 100C9xh, read in the same run, returned
  values. Whether those FFFFFFFF are register contents or failed reads is
  not known.

So the card was lost during the region run, the first access through
the + 192 MiB window, or as a late effect of `pre`. The BAR1 write test
(`2026-11-11-gt218-bar-windows.md`) measured 16, 32, 64 and 128 MiB and
256 MiB - 4 KiB, one 4 KiB page each; 192 MiB was never measured.

## Run 2 (boot 84): the machine stopped again, sooner

The probe now reads PCI configuration dwords 0 and 4 after every DIOC
run, splits `pre` into `pre` (PMC, PBUS, PFIFO and VM reads only),
`pre-userd` and `pre-gr` with a one-dword BAR1 check after each, and
stops after region (`/stop:region`). Run with the window still at
+ 192 MiB.

The agent logged `exec-start` at 15:03:07 (guest clock) and nothing more
on boot 84; boot 85's agent started at 15:06:06. The INI on boot 85 was
run 1's file unchanged: not even the EXE's DeleteFile and first writes
reached the disk, so the machine stopped within the lazy write delay of
starting, in `pre` or the first BAR1 check. Whether the owner or
something else reset it is not known (no chat reply).

Run 2's first runs are a subset of run 1's `pre` reads, then the first
access through the + 192 MiB window. Both runs lost the card at that
first window access. That is where the evidence points; it is not yet a
measurement.

## Run 3

The window moves to BAR1 + 128 MiB (a measured page, just past the
display driver's 128 MiB map), and each step goes first to
`C:\V9XDIAG\V9XN5CH.LOG`, written through and committed, so a reset keeps
the last step.

## Run 3 (boot 85): the USERD reads lose the card

Window at BAR1 + 128 MiB, write-through step log. The machine stopped
again and was reset (boot 86, about nine minutes later); the log and the
INI both survived. `V9XN5CH.LOG` (GetTickCount, step):

```
268280 run pre             268281 returned pre       268281 healthy pre
268282 run vram-pre        268283 returned vram-pre  268283 healthy vram-pre
268284 run pre-userd       268304 returned pre-userd 268305 healthy pre-userd
268306 run vram-pre-userd  268310 returned vram-pre-userd
```

INI `[Health]`: Cfg0 `0A6510DE`, Cfg4 `00100007` after `pre`, `vram-pre`
and `pre-userd`; **`FFFFFFFF` / `FFFFFFFF` after `vram-pre-userd`**, so
`Result=CARD-LOST-AFTER-vram-pre-userd`. `[Vram] vram-pre=ok`
(`C0DE0001` written and read back at BAR1 + 128 MiB + 4 MiB - 4 KiB).
`[PreUserd]` IbGet, IbPut, Ref = `FFFFFFFF`.

What it shows:

- BAR1 at + 128 MiB stores and returns a word with the card healthy.
- The three USERD reads (BAR0 C02088h, C0208Ch, C02050h) for channel 1,
  which PFIFO has not been told about, took about 20 ms between them
  (the other runs take 1 ms) and returned FFFFFFFF. The card was still in
  configuration space straight after.
- The next access, a BAR1 write and read, ended with the card gone from
  configuration space. The machine stopped some time after the EXE
  exited.
- Run 1 made the same USERD reads in `pre` and lost the card at its next
  BAR1 access (`region`). Its FFFFFFFF PGRAPH reads came after the USERD
  reads in the same run, so they are most likely failed reads too, not
  register contents.

Hypotheses this kills: the raw TLB flush (never ran) and the 192 MiB
window (run 3 lost the card at 128 MiB the same way). Not shown: why an
unbound channel's USERD read wedges the card, or whether a bound one is
safe. nouveau only touches USERD through a channel it has created.

Run 4 makes no USERD access before the channel is bound and enabled.

## Run 4 (boot 86): no USERD read, the card stays

Same build as run 3 less the `pre-userd` run, `/stop:region`. Exit 0,
`Result=STOPPED-AFTER-REGION`; configuration dword 0 `0A6510DE` after all
seven runs; every BAR1 check ok. The machine kept running.

With no USERD read before them, the PGRAPH reads return values:
INTR 0, TRAP 1, STATUS 0 (idle), 40032Ch 0, trap status 400804h
`80000003`, 400808h/40080Ch/400814h 0. Run 1's FFFFFFFF for the same
registers were failed reads.

The region readback matches what nv50_chan.c built, through BAR1 + 128 MiB:
PDE 0 `08010001`, RAMFC + 80h `1C000600`, the region's own PTE `08000381`,
the DMA object `7FC0003D`.

## Run 5 (boot 86): the kick stops the machine

Run 4's build, `/stop:fifo`. Every run through `fifo-bind` returned with
the card healthy: `fifo-init` (PFIFO reset, nv50_fifo_init constants,
channel table cleared, caches on) and `fifo-bind` (channel 1 entry
`80080054`, runlist at video memory 128 MiB + 20000h, count 1;
`RunlistWait=0`, the update was taken at once). The log's last line is
`run fifo-kick`; boot 87 followed. That run held the IB PUT write to
USERD (C0208Ch), a bounded wait reading IB GET (C02088h), and the status
reads including USERD; it cannot say which one stopped the machine.

Run 6 splits it: `fifo-put` (the write, a delay, PFIFO and VM status only),
`fifo-get` (one read each of IB GET and REF_CNT), `fifo-status`, with the
card and BAR1 checked after each.

## Run 6 (boot 87): the USERD write is survived, the USERD read is not

`/stop:fifo` with the kick split. Log (GetTickCount, step):

```
176136 returned fifo-put        176136 healthy fifo-put
176138 run vram-fifo-put        176141 returned vram-fifo-put  176142 healthy vram-fifo-put
176143 run fifo-get             176148 returned fifo-get       176148 healthy fifo-get
```

and nothing more; boot 88 followed. The IB PUT write to USERD, a delay and
PFIFO status reads left the card healthy, and a BAR1 write and read after
them worked. Two USERD reads (IB GET, REF_CNT) with channel 1 bound
returned in 5 ms with the card still in configuration space; the machine
stopped before the next step's first log line. The INI's sections after
`[Fifo]` were lost to the lazy write cache (the FifoPut status included).

So a USERD read stops the machine whether channel 1 is unknown to PFIFO
(run 3) or bound and kicked (run 6). nouveau reads IB GET through the same
BAR0 address (`nv50_chan_userd`, base C00000h, 2000h per channel) on this
family, so something about this channel or PFIFO's state differs from a
nouveau channel; not known yet what.

Run 7 stops after the PUT write (`/stop:put`) with no USERD read, dumps
CACHE1 (3200h-32FCh, a new VxD read window) and channel 1's RAMFC from
video memory, and copies every value to the write-through log.

## Run 7 (boot 88): the kick alone stops the machine

`/stop:put`, no USERD read at all; the put run also read CACHE1
3200h-32FCh and channel 1's RAMFC. The log's last line is
`run fifo-put` (boot 89 followed): this time the put run itself never
returned, where in run 6 the same put run (without the dumps) returned
and the machine stopped ~70 ms after the kick, during the next run.

Read together, runs 5 to 7 point at the kick, not at USERD reads: the
machine stops a varying 60-100 ms after IB PUT is written, whatever the
CPU does in the meantime. (Run 3's loss, a USERD read with no channel at
all, is separate and stays unexplained.)

Checked against nouveau and not the cause: the PTE (`08000381`: valid,
VRAM aperture 0, `log2blk` 7, the same encoding as nv50_vmm_pgt_pte) and
the PDE (`08010001`: 64 KiB pages, VRAM target, nv50_vmm_pde), so the
fetch cannot have been sent to host memory by the page tables.

The candidate run 8 tests: PGRAPH was uninitialised. The probe did
fifo-init, bind and kick before nv50_gr_init and the context program;
nouveau initialises every engine before it creates a channel, and its
nv50_ectx_bind notes that PFIFO "will hang forever if the connected
engines don't report that they've processed the context switch
request". Run 8 runs PGRAPH reset, nv50_gr_init and the context program
upload between fifo-init and the channel bind, and after the put waits
about a second (PTIMER reads only) before checking the card.

## Run 8 (boot 89): PGRAPH initialised first, the kick still stops the machine

PGRAPH reset, nv50_gr_init and the context program upload moved between
`fifo-init` and `fifo-bind`. They ran with the card healthy: after
`gr-init`, PGRAPH INTR 0, TRAP 0, STATUS 0, trap status `80000000`;
PFIFO caches 1, CACHE1 push 1, pull `70000001`. Then `run fifo-put` was
the last log line; boot 90 followed. **Killed: uninitialised PGRAPH as
the cause.**

## Run 9 (boot 90): the GPU sees the region where BAR1 put it

New stage, no kick: 1700h (PRAMIN base) read `00001FF0` (the VBIOS had
PRAMIN on the last MiB of the 512 MiB), set to `0800` (video memory
128 MiB), twelve words of the region read through BAR0 700000h, 1700h
restored. All twelve matched what BAR1 wrote: engine area 0, PGRAPH
context pointer `00190000`, DMA object `7FC0003D`, PDE 0 `08010001`,
RAMFC + 48h `00000020`, + 50h `08021000`, + 54h `00090000`, + 80h
`1C000600`, + 98h `00008000`, the region's PTE `08000381`, the runlist
entry `00000001`, push word 0 `00040050`. **Killed: BAR1 + x not being
video memory x** (at 128 MiB). The RAMFC, runlist commit (32F4h =
`8020`), IB entry (`(dwords) << 10`, nvif_chan506f) and fifo init writes
were also checked line by line against nouveau and match.

## Run 10 (boot 90): the flush page is host address 0

The VM block 100C00h-100C9Ch read in pre: **100C08h = `00000000`**.
nouveau always writes 100C08h with the bus address >> 8 of a host page it
allocates, the sysmem flush page (fb\nv50.c,
nv50_fb_sysmem_flush_page_init; nouveau ties a missing one to IOMMU
"read from address 0" errors). Left at 0, any GPU flush to it touches
host physical address 0, the real-mode interrupt table. Also read:
100C00h `FC84418B`, 100C0Ch `00F00002`, 100C20h `00430043`, 100C24h
`00000004`, 100C2Ch `00400040`, the rest of 100C10h-100C8Ch 0.

## Run 11 (boot 90): a flush page

N5CH.VXD's new op allocates one fixed, zeroed, contiguous page below
4 GiB (`_PageAllocate`) when 100C08h is 0 and writes its address >> 8
there; the page is never freed, since the GPU may keep using it. With
`/stop:region`: 100C08h `00000000` before, **`007FC970` after** (host
page 7FC97000h), card healthy, BAR1 check ok.

Run 12 kicks with the flush page in place.

## Run 12 (boot 91): the flush page is not enough

The kick with 100C08h on an allocated page. The put run returned this
time, but after 483 ms (run 6's put run, without the CACHE1 sweep, took
57 ms), and the machine stopped before the health check logged.

## Run 13 (boot 91): the kick works; reads that time out lose the card

The put run's raw results logged straight after the DIOC returned
(op order: the PUT write, a 1000-read delay, the known status reads, a
sweep of CACHE1 3200h-32FCh, a BAR1 read of channel 1's RAMFC):

```
PMC_ENABLE DFF3F113  PMC_INTR 0  PFIFO_INTR 0  caches 1
2604h 80080054  32ECh 1  3200h 1  3250h 70000001  100C90h-9Ch 0 0C18050E 00F00002 00000200
CACHE1 3200h 1        3204h 00010001  3208h-320Ch FFFFFFFF  3210h 0  3214h 10
       3220h 00504011 3224h 403F6078  322Ch 00000020        3260h 7FFFFFFF
       32E4h 4000     32ECh 1  32F0h 1  32F4h 8020          about 38 others FFFFFFFF
RAMFC through BAR1: all FFFFFFFF
```

The run took 385 ms; then `CARD-LOST-AFTER-fifo-put`.

What it shows:

- **The kick works.** PFIFO switched channel 1 into CACHE1: 3204h names
  channel 1, and 3224h, 322Ch and 3260h hold RAMFC + 3Ch (`403F6078`),
  + 48h (`20`) and + 60h (`7FFFFFFF`), loaded from the RAMFC the probe
  built.
- About 38 CACHE1 registers returned FFFFFFFF, and the run took hundreds
  of milliseconds where one of known registers takes one: reads that do
  not complete. After them BAR1 read FFFFFFFF and the card was gone from
  configuration space.

Read with the earlier runs: run 3 lost the card after USERD reads with no
channel, run 6 after USERD reads with the channel kicked (the kick itself
and known registers were survived), and runs 7, 8, 12 and 13 had the
CACHE1 sweep in the put run. **A BAR0 read that times out is what loses
the card**, not the kick. The earlier readings of runs 5 to 12 as "the
kick stops the machine" are superseded. Whether a USERD read is ever
valid on this card from here (nouveau reads IB GET there) is not known.

## Run 14

No USERD read and no CACHE1 sweep; N5CH.VXD no longer accepts either.
The fifo push becomes REF_CNT, `SET_CONTEXT_DMA_SEMAPHORE` (60h) = the VM
DMA object, then SEMAPHOREA-D releasing `FEED0001` at video memory
128 MiB + 3F0000h; the fetch is judged by reading that word through BAR1.
The flush page stays.

## Run 14 (boot 92): the machine stops again, at the first BAR1 poll

`/stop:fifo`. The put run (PUT write, 1000-read delay, known registers)
returned at once with the card healthy: 2604h `80080054`, 32ECh 1, CACHE1
push 1, pull `70000001`, PFIFO_INTR 0, PMC_INTR 0, no VM fault (100C90h
0). The next run, `fifo-sema`, a bounded poll of the semaphore word
through BAR1 (up to 1,000,000 reads), never returned; boot 93 followed.

So the reading of run 13 is not the whole story: with no BAR0 read that
could time out, the machine still stopped within a second of the kick.
Two readings remain and this run does not separate them:

1. Something the push does is harmful: the semaphore release (methods
   60h and 10h-1Ch, new in this run) or the REF_CNT method, if PFIFO's
   view of the DMA object or VM sends the write outside video memory.
2. PFIFO wedges the card a short time after any kick, and run 6 (which
   survived a delay and a BAR1 check after the kick) only beat it.

## Where this leaves step 3 (paused 2026-11-12)

Eleven resets of a remote-help machine in one night, each needing its
owner to power-cycle it. The kick experiments are paused for review
before any more. Established on this card, by measurement:

- PMC/PFIFO/PGRAPH registers that answer, BAR1 at + 128 MiB, PRAMIN
  through 1700h (restored), nv50_fifo_init, nv50_gr_init and the context
  program upload, channel bind and runlist commit: all survive.
- The GPU sees the channel region exactly where BAR1 wrote it.
- The kick loads channel 1's RAMFC into CACHE1.
- BAR0 reads that time out (USERD, unanswered CACHE1 registers) are
  followed by the card leaving configuration space.
- 100C08h (sysmem flush page) is 0 after the VBIOS; the probe now sets
  it, as nouveau does. Necessary or not is not known.

Next experiment, proposed, not run: the kick with an **empty** push (one
IB entry of length 0, or a NOP-only push), then only a delay and the
configuration-space check. If the machine survives, the methods are the
cause (bisect REF_CNT against the semaphore); if not, PFIFO's handling
of the kick itself is, and the next suspects are the RAMFC words nouveau
derives from objects the probe does not model (0x3C, 0x44, 0x60, 0x7C).

## Run 15 (boot 93): an empty push stops the machine too

Michael approved the experiment above (2026-11-12). `/push:empty
/stop:fifo`, build 8cac3715-dirty: the IB entry points at a pushbuffer of
one word, 0 (method 0, count 0, nothing to execute). After the put run,
the probe sleeps a second on the host without touching the card, then
checks configuration space and BAR1, then reads the known registers.
Log: `build/driver-results/m-bxmfa8-gt218-boot84/run15-V9XN5CH.LOG`
(the file accumulates across boots; run 15 starts at its line 1773).

- Everything up to the bind passed as in runs 12-14; the flush page was
  set again (100C08h `007FCCD0`).
- The put run returned in about 1 ms (run 12's took 483 ms): 2604h
  `80080054`, 32ECh 1, CACHE1 push 1, pull `70000001`, PFIFO_INTR 0,
  PMC_INTR 0, 100C90h 0.
- **PCI configuration dword 4 read `20100007` straight after the put**,
  where every earlier stage of the run read `00100007`. Bit 29 is status
  bit 13, *received master abort*: the card, as bus master, issued a
  transaction no target claimed. Run 14's log shows the same value at the
  same point; it was not noticed then. Runs before 13 logged no dword 4,
  so whether the bit appears without a flush page is not known.
- After the one-second sleep, configuration space still answered (vendor
  and device, status still `2010`), but the BAR1 marker at + 3FF000h read
  FFFFFFFF, and the next VxD run refused its first op. The EXE exited
  (`FIFO-REFUSED`); the machine stopped shortly afterwards and came back as
  boot 94 after the owner's power-cycle.

So the methods are not the cause. Pushing nothing is enough. Within a
second of the kick, video memory stops answering through BAR1 while
configuration space still answers. The master abort is the first sign
that is not a timeout: PFIFO, or something it starts, sends a bus-master
access to a host address nothing decodes.

Where the card can address the host here, by what the probe set up:

1. **The flush page (100C08h).** `_PageAllocate` returned physical pages
   just under 2 GiB (`7FC97000`, `7FCC9000`, `7FCBC000`, `7FCCD000`). If
   this machine has less memory than that, or Windows reports pages it
   cannot back, the GPU's flush read master-aborts. The machine's RAM size
   has not been read.
2. **A VM or DMA object that resolves to system memory.** The PTEs (aper
   0), the PDE (target 0), the VM DMA object (`7FC0003D`, target VM) and
   the PGRAPH pointer (`00190000`) were checked against nouveau after this
   run and match. No word written by the probe names a host target, but
   the RAMFC words copied from nouveau (3Ch, 44h, 60h, 7Ch) are not
   decoded.

The next run, 16, separates (1) from the rest. `/flush:none` skips the
allocation, so 100C08h keeps the 0 the VBIOS left (host address 0, real
memory). The probe also logs `GlobalMemoryStatus`'s total. If the master
abort goes, the flush page address is the fault. If it stays, it does
not, and (2) is next.

## Run 16 (boot 94): no flush page, the same master abort

`/push:empty /flush:none /stop:fifo`, build 31003553. Log:
`run16-V9XN5CH.LOG` (run 16 starts at its line 2079).

- `GlobalMemoryStatus` total `7FEE5000` (2047 MiB): the machine has 2 GiB,
  so the earlier flush pages near 2 GiB were real memory.
- 100C08h read 0 and was left at 0. Every stage up to and including the
  bind read dword 4 `00100007`.
- The put run returned at once, with the same state as run 15 (2604h
  `80080054`, CACHE1 push 1, pull `70000001`, no PFIFO, PMC or VM fault).
  **Dword 4 read `20100007` straight after it**: the master abort again.
- After the one-second host sleep, configuration space read FFFFFFFF
  (`CARD-LOST-AFTER-fifo-sleep`). The machine stopped; boot 95 followed
  the owner's power-cycle.

So the flush page is not the address nothing answered: reading (1) of
run 15 is dead. With nothing to execute, the kick still makes the card
issue a bus-master cycle to an unclaimed host address, and the card is
gone within a second. The page tables, the VM DMA object and the PGRAPH
pointer were checked against nouveau and name no host target. What is
left is the hardware's own reaction to a channel switch-in on this
setup: the RAMFC words copied from nouveau without decoding (3Ch, 44h,
60h, 7Ch), PFIFO state nouveau sets up elsewhere that the probe does
not, or a host-targeted save of the context that was current before
(PFIFO's or PGRAPH's). None of these has been tested.

## Source study after run 16 (2026-11-12, no run)

Michael asked for a read of the saved nouveau sources
(`build/external/nv50/nouveau/`) before another kick. I compared what
nouveau sets up for chipset A8 (`nva8_chipset`: gt215_fb, gt215_mc,
g84_bar, g84_mmu, nv50_instmem, g98_fifo, gt215_gr) with what the probe
does. Nothing below has been measured.

**The VM fault readings so far mean nothing.** `nv50_fb_init`
(subdev_fb_nv50.c) writes 100C90h = `fb->func->trap`, `000D0FFF` for
gt215 (subdev_fb_gt215.c), with the comment that it "is needed to get
meaningful information from 100c90 on traps". The probe never writes it,
and the VBIOS leaves it 0. 100C94h-100C9Ch read the same in every stage of
a run and change only between boots (`0C18050E` in run 15, `0C10050E` in
run 16). So "no VM fault (100C90h 0)" in the run 12-16 sections above says
only that trap reporting was never enabled. It is not evidence that the
fetch stayed in video memory. nouveau's trap decoder (`nv50_fb_intr`,
same file) shows how to read a trap: if 100C90h bit 31 is set, write
`idx | i << 24` for i = 0-5 and read 100C94h each time. Word 5 bit 8 is
read/write, words 3-5 give the 40-bit address, words 1-2 the instance,
and word 0 the engine, client and reason.

Other differences, in the order I would test them:

1. **PMC_ENABLE.** `nv50_mc_init` (subdev_mc_nv50.c) writes 200h =
   `FFFFFFFF`. The probe keeps the VBIOS's `DFF3F113`, and the VxD lets
   it change only the PFIFO and PGRAPH bits. The bits that are off
   (`200C0EEC`: 29, 19-18, 11-9, 7-5, 3-2) are all outside gt215's
   reset table (subdev_mc_gt215.c), so the saved sources do not name
   their units.
2. **The BAR channel.** nouveau always programs 1704h with an instance
   block of its own (`nv50_bar_bar2_init`: written once plain, then with
   `40000000h`), sets 1708h and 170Ch, and clears 1900h-191Ch
   (`nv50_bar_init`). Here 1704h, 1708h and 170Ch are 0. The saved
   sources do not say whether PFIFO needs 1704h.
3. **The empty runlist first.** `nv50_fifo_init` commits an empty runlist
   before it enables CACHE1 (3200h, 3250h, 2500h). The probe commits only
   when it binds.

Checked and not a difference: nouveau never flushes a PFIFO TLB
(`nv50_vmm_flush` has no PFIFO case), so the probe is not missing one.
RAMFC 44h is the same value nouveau writes to 2044h, and CACHE1 3224h
loaded 3Ch's value in run 13, so both reach the hardware as written.

Proposed run 17, before testing anything above: enable the trap
(100C90h = `000D0FFF`) during fifo-init. Then, in the put run, straight
after the kick and its delay, read 100C90h and, if bit 31 is set, the six
trap words. This needs the VxD to allow a write to 100C90h; it allows
none today. The put run's results came back each time before the card
was lost, so a trap the kick causes should be in them. That would name
the address the card was reaching for, rather than another guess.

## Run 17 (boot 95): the trap enabled, and nothing trapped

`/push:empty /flush:none /stop:fifo`, built from 0b31d1a6 plus the trap
change, so the build id reads `0b31d1a6-dirty`; the code is what
8545c6bb committed. Log: `run17-V9XN5CH.LOG` (run 17 starts at its line
2359).

- The enable stuck: 100C90h read `000D0FFF` after the write in
  fifo-init.
- In the put run, after the kick and its delay, 100C90h read `000D0FFF`:
  **bit 31 clear, no trap**. The six words came back as `0C18050E
  00003FC5 000039FF 0000924C 0000DD44 00000003`. The select writes did
  change what 100C94h returns. Word 0 is the value earlier runs read as
  "100C94h" every boot, so that was trap word 0 of a stale or reset
  state, not a live reading. With bit 31 clear, none of the words
  describe this kick.
- PCI status read `20100007` (master abort) straight after the kick, as
  in runs 14-16. After the one-second host sleep, configuration space
  read FFFFFFFF (`CARD-LOST-AFTER-fifo-sleep`). The machine stopped, and
  boot 96 followed the owner's power-cycle. The owner was asked through
  their other remote-help machine, M-JJN1ZZ, because the frozen one
  cannot receive chat.

So with VM trap reporting on as nouveau has it, the kick causes no MMU
fault, yet the card still issues a bus-master cycle nothing claims. Either
the access never goes through the channel's VM, or it translates without
a fault to a host target. The probe's own tables name no host target. An
access by physical address fits the first reading. The BAR channel
(1704h, 0 here, always programmed by nouveau) is the untested difference
that could hold such an address. PMC_ENABLE is the other.

## Source study of the BAR channel (2026-11-12, no run)

What the saved nouveau sources say about 1704h, from subdev_bar_nv50.c
and nvkm_subdev_bar_g84.c (g84_bar is A8's):

- `nv50_bar_oneinit` allocates a 128 KiB instance block of its own
  (`bar->mem`). g84 reserves 200h bytes before its 16 KiB page directory
  (`pgd_addr` 200h; nv50 uses 1400h). It joins two VMs to that block,
  "bar2" (nouveau's name for BAR3, from VM address 1_0000_0000h) and
  "bar1" (from 0). Each gets a 24-byte DMA object in the block: flags
  `7FC00000`, limit, start, then limit high << 24 | start high, then 0, 0.
- `nv50_bar_bar2_init` writes 1704h = `mem >> 12`, then 1704h =
  `40000000h | mem >> 12`, then 170Ch = `80000000h | bar2 DMA offset >>
  4`. `nv50_bar_bar1_init` writes 1708h = `80000000h | bar1 DMA offset >>
  4`. `nv50_bar_init` clears 1900h-191Ch. What bit 30 of 1704h means is
  not in the sources.
- **Who uses it**: only the BARs themselves. Instmem goes through the
  bar2 VM (`nv50_instobj_bar2`), and USERD needs BAR1 only on chips whose
  USERD lives behind BAR1 (`nvkm_fifo_oneinit`). NV50's USERD is BAR0
  C00000h (`nv50_chan_userd`). No PFIFO, PGRAPH or MMU code in the saved
  sources reads 1704h or depends on it. So the sources give no reason
  why a channel kick would need a BAR channel. Whether the hardware
  needs one anyway, they cannot say.

One finding is not about 1704h itself. **nouveau flushes the BAR write
path before the GPU reads what the CPU wrote.** `nv50_instobj_release`
(subdev_instmem_nv50.c) does `wmb()` then `nvkm_bar_flush`, which on G84
is `g84_bar_flush`: write 70000h = 1, then wait up to 2 s for bit 1 to
clear. Every instance-memory write batch ends this way, including the
RAMFC, the page tables and the DMA objects. The probe writes all of these
through BAR1 and never writes 70000h. The evidence that the writes still
arrived: PRAMIN read back what BAR1 wrote (run 9), and CACHE1 loaded the
RAMFC words (run 13). But PRAMIN may see a path the PFIFO and MMU fetch
does not, and only twelve words were checked. A missing BAR flush is
the one step nouveau always does before a channel runs that the probe
does not.

Proposed run 18, the two steps nouveau does that the probe does not, in
one run because each run costs a power-cycle:

1. After the region is written and before the bind, the G84 BAR flush:
   70000h = 1, a bounded wait for bit 1 to clear. Its result is a finding
   on its own. A flush that never completes would say the write path
   was not idle.
2. A BAR channel with the BARs left linear: an instance block in the
   region with an empty page directory, 1704h written as nouveau writes
   it (plain, then | `40000000h`), and 1708h and 170Ch left 0, so BAR1
   and BAR3 keep decoding linearly. The desktop lives in BAR3 linear, so
   this keeps it out of the change. Read 1704h back before the kick.

If run 18 survives, a run with each step alone says which mattered. If
it does not, PMC_ENABLE is the last of the differences nouveau's init
order shows.

## Run 18 (boot 96): BAR channel and BAR flush, the same master abort

`/push:empty /flush:none /stop:fifo`. The build id reads `1407d7a6-dirty`
because it was built before the commit; the code is 7836ca4a's. Log:
`run18-V9XN5CH.LOG` (run 18 starts at its line 2649).

- Bar stage: 1704h read `00000000` before. After the two writes it read
  `40008100`, which is bit 30 | video memory 129 MiB >> 12, as written.
  The G84 BAR flush finished on the first poll (FlushWait 0, 70000h `0`).
  PCI status stayed `00100007`, and the BAR1 marker read back.
- fifo-init, gr-init and the bind passed as before, and the trap enable
  read back `000D0FFF`.
- After the kick: PCI status `20100007` (master abort), 100C90h
  `000D0FFF` (no trap), 2604h `80080054`. The same as runs 15-17.
- The log ends at `sleep 1000`, the host sleep after the put run. The
  health check after it never reached the log. In runs 16 and 17 that
  check read FFFFFFFF; here the machine stopped first, or during the
  configuration read itself. Boot 97 followed the owner's power-cycle,
  requested through M-JJN1ZZ.

So neither step nouveau takes before a channel runs changes anything:
the BAR channel and the flush of the BAR write path are ruled out with
the flush page (run 16) and VM faults (run 17). The flush completing at
once also says the BAR1 writes were not pending when the kick came.

Left from the source comparison: PMC_ENABLE (`DFF3F113` here,
`FFFFFFFF` in `nv50_mc_init`) and the empty runlist that
`nv50_fifo_init` commits before enabling CACHE1. Neither has a mechanism
in the sources that would explain a bus-master cycle to the host. The
probe is now at the end of what reading nouveau suggests. Step 3 should
pause again here for a decision on what to try next.

## Run 19 (boot 97): PMC_ENABLE cannot be raised; the kick unchanged

Michael chose to try PMC_ENABLE anyway (2026-11-12). `/push:empty
/flush:none /stop:fifo`, with the fe496fd1 code built as `cfbde3cd-dirty`.
Log: `run19-V9XN5CH.LOG` (run 19 starts at its line 2956).

- The new mc stage wrote 200h = FFFFFFFF, waited 1000 PTIMER reads and
  read **`DFF3F113`**. The VxD accepted the write (run status 17, as for
  every accepted run), and the card kept the old value. The bits left off
  (`200C0EEC`) do not stick on GT218. With nothing behind those bits to
  switch on, every unit that can be enabled already was. nouveau's
  `FFFFFFFF` amounts to the same state, so PMC_ENABLE is not a
  difference after all.
- The BAR stage repeated run 18 (1704h `40008100`, flush at once), and
  fifo-init, gr-init and the bind passed.
- After the kick: PCI status `20100007` (master abort), no VM trap,
  2604h `80080054`. After the one-second host sleep, configuration space
  still answered (`0A6510DE`, status `2010`), but the BAR1 marker read
  FFFFFFFF and the next VxD run refused its first op. This is the same
  sequence as run 15. The machine then stopped; boot 98 followed the
  owner's power-cycle, requested through M-JJN1ZZ.

So the comparison with nouveau's init is exhausted: the methods, the
flush page, VM faults, the BAR channel and BAR flush, and PMC_ENABLE
are all ruled out. The empty runlist that `nv50_fifo_init` commits
before enabling CACHE1 is the only difference left untested, and nothing
in the sources links it to a host bus-master cycle. What runs 15-19
established, every time: within a millisecond of the IB PUT write, with
nothing to execute, the card issues a bus-master cycle no host target
claims, and within a second video memory stops answering BAR1, then
the card or the machine is lost.

## Run 20 (boot 98): the empty runlist first, no change

Michael chose to try it (2026-11-12). `/push:empty /flush:none
/stop:fifo`, the 625859c1 code, built as `70fe4a5c-dirty`. Log:
`run20-V9XN5CH.LOG` (run 20 starts at its line 3301).

- fifo-init committed an empty runlist (32F4h, 32ECh = 0) before
  enabling CACHE1. The update completed on the first poll
  (`Fifo.EmptyRunlistWait` 0). The bind's runlist update completed the
  same way.
- After the kick: PCI status `20100007`, no VM trap, 2604h `80080054`,
  32ECh 1. After the one-second host sleep, configuration space answered
  but BAR1 read FFFFFFFF and the next VxD run refused its first op, as in
  runs 15 and 19. The relay dropped the session, and boot 99 followed the
  owner's power-cycle.

Every difference from nouveau's init order that the saved sources show
is now tested and ruled out. Reading nouveau has no more to give. Ways
forward that do not depend on it, recorded for Michael's choice:

1. Read the GPU's own PCIe extended configuration space through its BAR0
   mirror (88000h + 100h onwards) after the kick. If it implements AER,
   its header log may hold the unanswered request and its address. Reads
   only, one run.
2. The root port's error status, through MMCONFIG if the chipset
   exposes it under Win98. Unchecked.
3. A register trace of Linux nouveau bringing up a channel on this card
   (mmiotrace), which needs the owner to boot Linux on M-BXMFA8.
4. Stop at the working tier: the VBE desktop with the BAR1 DirectDraw
   heap (boot 83), the CPU doing fills and copies.

## Run 21 (boot 99): the PCIe error state, no address

Michael chose (1) (2026-11-12). The 52ede5ef code, built as
`0dd01736-dirty`, `/push:empty /flush:none /stop:fifo`. Log:
`run21-V9XN5CH.LOG` (run 21 starts at its line 3647).

- **The BAR0 configuration mirror works**: 88000h-8803Ch matched the
  CF8h reads of 00h-3Ch, 16 of 16, and reading 88000h-883FFh did not
  disturb the card.
- **No AER in 100h-3FFh.** The extended list is 100h `12810002`
  (Virtual Channel, next 128h), 128h `60010004` (Power Budgeting, next
  600h). 600h onwards was not read.
- The card's PCIe capability is at 78h. Its Device Control/Status read
  `00002810` before the run, after the bind and after the kick: Device
  Status 0, so the card detected no error as a receiver. It has
  error-reporting enables off (Device Control bits 3:0 clear), but the
  status bits do not depend on them.
- The bridge above it is bus 0 device 1 function 0 (`80000800`), with a
  PCIe capability at A0h. Its status `0010`, Device Control/Status
  `00000000` and Root Status `00000000` read the same at every point,
  so the root port logged no Unsupported Request. Its secondary status
  read `2000` (received master abort) **before the run started**. That is
  most likely from Windows' or the BIOS's scan of empty device numbers on
  bus 1, so it says nothing about the kick.
- The kick: PCI status `20100007` again, no VM trap, 2604h `80080054`.
  The log ends at the one-second host sleep, as in run 18. Boot 100
  followed the owner's power-cycle.

So the card received a master abort that the root port above it did not
record as an Unsupported Request it detected, and the card has no AER
below 400h to log the request in. The PCIe logs reachable from here do
not name the address. Untried: the extended list from 600h (probably
NVIDIA's own capability), and clearing the bridge's secondary status
before the kick so that a fresh bit 13 would be attributable to it.

## Run 22 (boot 100): an unmapped IB traps, and the card survives

A control, run while the local GT210 is not yet in the BringupKit target
([nvidia-gt218-linux-trace.md](../plans/nvidia-gt218-linux-trace.md)).
`/push:empty /flush:none /ib:unmapped /stop:fifo`, the 7e8a4950 code.
The RAMFC's IB address (RAMFC + 50h) is moved to 30000000h, above the
512 MiB PDE 0 maps; PDE 1 is zero. Log: `run22-V9XN5CH.LOG` (from line
4001).

- **100C90h `800D0FFF`: a VM trap**, the first in 22 runs. Words 0-5:
  `00080F05 00008000 00000000 00000000 00003000 00000100`, decoded as
  `nv50_fb_intr` does for A8:
  - word 5 bit 8 says it was a read;
  - word 5 bits 7:0, word 4 and word 3 give the address,
    **30000000h, the IB address written**;
  - (word 2 << 16 | word 1) << 12 gives the instance, **08000000h, this
    channel's instance block**;
  - word 0's bytes are engine 05h, reason 0Fh and client 08h. They are
    not decoded here; nouveau's enum tables are not in the saved sources.
- PMC_INTR `00000100` (PFIFO), PFIFO_INTR `00001000` (DMA pusher).
- **PCI status stayed `00100007`: no master abort.** After the one-second
  sleep the card answered, the BAR1 marker read back, and the probe ran
  to its end (`EMPTY-PUSH-SURVIVED`). The machine kept running.

So PFIFO reads the IB through this channel's VM: our instance block, page
directory and page tables. A fault there is reported and survived. The
master abort in runs 14-21 comes after an IB fetch that succeeds.

## Run 23 (boot 100): an unmapped push neither traps nor survives

Same boot, `/push:empty /flush:none /push:unmapped /stop:fifo`, the
0e2ad5e4 code built as `7e8a4950-dirty`. The IB stays mapped; its entry
names a pushbuffer at 30000000h. fifo-init now acknowledges a stale trap
first: it found run 22's (`850D0FFF`) and cleared it, and the enable
read back `000D0FFF`. Log: `run23-V9XN5CH.LOG` (from line 4393).

- After the kick: 100C90h `000D0FFF`, **no trap** (the words are run 22's,
  still selectable). PFIFO_INTR 0, PMC_INTR 0, **PCI status `20100007`**.
- After the sleep, configuration space answered with the abort bit still
  set, BAR1 read FFFFFFFF, and the next VxD run refused. Boot 101
  followed the owner's power-cycle.

30000000h is outside the push DMA object's limit (1FFFFFFFh) as well as
outside the VM. A fetch through that object should fail its limit check
or fault in the VM, and neither was reported. And 30000000h is real
memory on this 2 GiB host, so even a fetch sent to the host at that
physical address would be answered. So the master-aborted cycle is
probably not the push fetch at that address. Something PFIFO does after
a successful IB fetch, and before or instead of the push fetch, sends a
cycle to a host address nothing decodes.

Compared with nouveau's Tesla push DMA object (nouveau_chan.c: target VM,
access VM, `NV_DMA_FROM_MEMORY`; usernv50.c: flags0 `7FC00002`, word 14h
`part << 16` = 0), the probe's differs only in class (3Dh, IN_MEMORY)
and limit (512 MiB against nouveau's VM limit). Neither is tested.

## Run 24 (boot 101): nouveau's push DMA class removes the master abort

Michael chose the class test (2026-11-12). `/push:empty /flush:none
/push:class02 /stop:fifo`, the e10041e8 code built as `c0be5177-dirty`.
The push DMA object's flags0 is `7FC00002` (NV_DMA_FROM_MEMORY, as
nouveau's Tesla push ctxdma) in place of `7FC0003D`; the region readback
read `7FC00002`. The IB and push are mapped. Log: `run24-V9XN5CH.LOG`
(from line 4774).

- Trap.Before 0 (fresh boot). After the kick: **PCI status `00100007`,
  no master abort**, for the first time after a kick that fetched a
  mapped IB. No VM trap (100C90h `000D0FFF`), PMC_INTR 0, PFIFO_INTR 0.
- After the one-second host sleep, PCI status was still `00100007`,
  configuration space answered, and the card's and the root port's PCIe
  status were unchanged. **But BAR1 read FFFFFFFF** and the next VxD
  run refused its first op, as in runs 15-23. The machine stopped; boot
  102 followed the owner's power-cycle.

So the class of the push DMA object caused the master abort in runs
14-23. With class 3Dh, PFIFO's push fetch turned into a host cycle
nothing decoded. That reading also fits run 23, where the push address
did not matter. With nouveau's class the cycle is gone. A second fault
remains: within a second of the kick, video memory stops answering BAR1
while configuration space still answers, with no interrupt and no trap
seen at about a millisecond. What happens in between is not yet
observed.

## Run 25 (boot 102): BAR1 goes first, BAR0 within 80 ms

As run 24 (`/push:class02`), with the host sleep replaced by short VxD
runs about 20 ms apart, each logged on return. The 2cf16d92 code was
built as `0ab3d5f4-dirty`. Log: `run25-V9XN5CH.LOG` (from line 5156).
GetTickCount stamps, in ms:

| Tick | Event |
| --- | --- |
| 327303-327304 | put run (IB PUT, 1000 PTIMER reads, status): PCI `00100007`, no trap, PFIFO_INTR 0 |
| 327350-327354 | sample 00: PTIMER `A0302F60`, PMC_INTR 0, PFIFO_INTR 0, 2500h 1, 2604h `80080054`, 3200h 1, 3250h `70000001`, 32ECh 1, 100C90h `050D0FFF` (no trap), PGRAPH_INTR 0, 400700h 0 (idle); **BAR1 word FFFFFFFF**; PCI `00100007` |
| 327384-327388 | sample 01: refused at op 0, status `00000001` (found, not identified): **PMC_BOOT_0 through BAR0 no longer read A8**; configuration space still `0A6510DE` / `00100007` |

So about 50 ms after the kick, video memory has stopped answering
through BAR1. Every BAR0 register still reads normally and shows nothing
wrong: PFIFO idle with the channel resident, PGRAPH idle, no interrupt,
no VM trap. 30 ms later BAR0 has stopped answering too. Configuration
space, which the bridge reaches without the card's memory path, still
answers. Run 22, where the IB fetch faulted and no push was fetched,
lost neither. The failure follows the push fetch and reaches the card's
host interface: first its framebuffer path, then its register path.

## Run 26 (boot 103): sampling inside the kick's run hangs it

As run 25, with 40 samples (PTIMER, the BAR1 instance word, PMC_INTR,
PFIFO_INTR, 400700h, then 2000 PTIMER reads) added to the put run itself,
straight after the IB PUT write. The 46fa90d3 code was built as
`505761c7-dirty`. Log: `run26-V9XN5CH.LOG` (from line 5529).

The log ends at `run fifo-put`: the put run never returned, so none of
its samples were recorded. The machine stopped; boot 104 followed the
owner's power-cycle.

Run 25's reads of BAR1 and BAR0 about 50-80 ms after the kick returned
FFFFFFFF. Some read in this run, made while the failure was in progress,
did not return at all. Which one, and when, is not known. Reading the
card's memory windows during the first tens of milliseconds after a kick
can stop the CPU, so a finer timeline from Win98 needs reads that cannot
stall the bus, and none of these qualify. This is where the measurement
from M-BXMFA8 stops being cheap. The plan for the local GT210 under
Linux nouveau ([nvidia-gt218-linux-trace.md](../plans/nvidia-gt218-linux-trace.md))
is the next step: it should show what nouveau does after its push fetch
that the probe does not.


