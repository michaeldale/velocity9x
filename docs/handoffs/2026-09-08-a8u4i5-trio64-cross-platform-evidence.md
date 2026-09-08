# A8U4I5 Trio64 lock: the same card, the same workload, a different host

Date: 2026-09-08

Scope: measured evidence from BringupKit bearing on
[A8U4I5 hard-locks on framebuffer read-after-write](../issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md).
The card was moved to a Linux target on a different chipset and driven through
the same class of workload under instrumentation.

**Headline: the card is exonerated. Interleaved CPU write/read to the Trio64's
linear framebuffer ran clean over 12,012,288 accesses on an AMD host, with no
lock, no data corruption and no PCI master abort.** That turns "the remaining
variable is the board's PCI configuration" from the last untested hypothesis
into the one the evidence now points at.

There is one dimension in which this result may not transfer, and it is the
dimension that matters most for a posted-write theory: **write-combining**. See
[the caveat](#the-caveat-that-matters-write-combining).

## The other host

| | A8U4I5 | The Linux target |
|---|---|---|
| CPU | Pentium III | AMD FX-6300 |
| Chipset | untested, PCI 2.0-era | AMD RS780 north / SB710 south |
| PCI attachment | on-board PCI | behind `SBx00 PCI to PCI Bridge` `1002:4384` |
| OS | Windows 98 SE | Debian 13, kernel 6.12.107 |
| Card | S3 Trio64 `5333:8811`, 2 MB | the same card |

Confirmed the same silicon two independent ways: the in-kernel `s3fb` driver
identified it as `S3 Trio64` with `2 MB RAM, 60 MHz MCLK`, and its option ROM
was captured byte-exact (32 KiB, Diamond Stealth64 DRAM 2.02).

Getting the framebuffer to answer at all took work worth knowing about: the card
returned all-ones from BAR 0 until `CR58` bit 4, the linear-addressing enable,
was set. Firmware POST leaves it clear because linear addressing is
driver-managed. With it set, reads returned recognisable VRAM — `0x20`
characters, attribute `0x07`, and a real 8x16 font glyph in the plane data.

## What was measured

Three modes, everything else held constant, on the same card in the same boot.
The middle column is the workload your table isolates as the trigger.

| Mode | Accesses | Mismatches | ns/access |
|---|---|---|---|
| write-only | 2,000,000 | 0 | **285** |
| read-only | 2,000,000 | 0 | **1622** |
| **interleaved write+read-back** | 2,000,000 | 0 | **2005** |

Plus interleaved at width 1, width 2, and stride 4096: 2,000,000 each, zero
mismatches. Total 12,012,288 accesses. Every run left the guarded PCI registers
unchanged, and the upstream bridge's Received Master Abort latch — cleared
before each run so a fresh abort would be attributable — stayed clear
throughout.

### Posting, quantified

This is the part that speaks directly to your hypothesis.

- A **write** costs 285 ns. It is posted: the CPU hands it off and does not wait.
- A **read** costs 1622 ns, 5.7x more. It is non-posted, so the CPU stalls for a
  full round trip to a 33 MHz device behind a bridge.
- **Interleaved** costs 2005 ns against 1907 ns for the two added together — a
  5% overhead, **not a pathological interaction**.

So on this host the posted-write drain your theory turns on is present and
measurable, and it is simply *additive*. Nothing stalls, retries or wedges.
Stride made almost no difference (2025 ns at stride 4096 against 2005 at stride
4). Width made none at all, which is itself informative: a byte access costs the
same as a dword, so the cost is per-transaction rather than per-byte.

## What this supports and what it does not

**Supports:** the card and the workload class are not inherently fatal together.
The same Trio64, driven by CPU stores and loads to its linear aperture at
several hundred thousand accesses per second, is clean. Your issue already
exonerated the engine, the driver and readback-alone; this exonerates the card
itself and leaves the platform.

**Does not support:** any claim that A8U4I5 is fine. A pass on an AMD board does
not disprove a Pentium III failure, and BringupKit's own plan says so. What it
does is convert your last row — "the remaining variable is the board's PCI
configuration" — from an untested guess into the surviving candidate.

## The caveat that matters: write-combining

Your issue notes A8U4I5 has **real MTRRs**. If the framebuffer aperture there is
mapped write-combining, its posted-write behaviour is materially different from
what was measured here: WC buffers coalesce and then burst, and a read that must
drain a WC buffer is a different transaction pattern from a read draining
individually posted stores.

The Linux runs used a plain `MAP_SHARED` mapping of the device's sysfs
`resource0` with no MTRR or PAT change, so **uncached, individually posted
writes**. That is the conservative case, and it is *not* the case most likely to
wedge a bridge.

So the single most valuable next experiment on A8U4I5 is not another BIOS toggle
— it is to establish whether the framebuffer is WC there, and if it is, to try
the harness with WC disabled on that range. If the lock disappears, the fault is
in the write-combined burst-then-drain pattern rather than in PCI settings at
all, and that is a different fix.

Two smaller differences worth noting: the harness draws GDI fills into a
320x240 window, so a mix of sequential and short-strided access, whereas these
runs used a configurable constant stride; and the Pentium III drives accesses at
a different rate and ordering than an FX-6300.

## What is available to reuse

BringupKit can drive this card on the Linux target under instrumentation, with
durable logs. Relevant to this issue:

- `vram-exercise` — the three-mode matrix above, with a deterministic pattern
  (`seed + index x 0x9E3779B9`) so any run is reproducible from its journal,
  and checkpoints that bound the suspect range if a run ever does wedge.
- `bar-read --detect-abort` — clears the bridge's master-abort latch and
  re-reads it, which turns "the bus wedged" into a measured claim about which
  side aborted. Note for your own instrumentation: that latch reads `0x2280`
  a few minutes into an ordinary boot, so **the abort bit is set during normal
  enumeration** and a bare reading of it proves nothing.
- `linear-window-enable` — puts the card into linear addressing from a known
  state and restores it, if you ever want the Linux side to match a specific
  configuration.
- netconsole hang capture, with the loglevel caveat: netconsole carries what
  goes to a *console*, not to the ring buffer, so at the default `quiet`
  loglevel of 4 only levels 0-3 transmit and the run-up to a hang is invisible.

If it would help, the same matrix can be re-run with a different stride,
delay-between-accesses, or a far longer run.

## One correction to our own course

BringupKit spent an afternoon reaching the Trio64's drawing engine — bridge I/O
window widening, `CR40` bit 0 for enhanced register access, the rectangle-fill
command sequence — in order to reproduce this issue with *engine* fills.

That was misdirected, and your own table says why: the three `V9XTC32 pump` runs
and the `GdiAccel=0` row already exonerate the engine. The trigger you isolated
is CPU access, and the CPU measurement above is the one that was actually
needed. The engine work is written up in BringupKit's `engine-fill-v1.md` for
whoever wants it, but it is not evidence about this issue.
