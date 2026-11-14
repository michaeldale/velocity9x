# GT218: GDI fill and copy on the 2D engine

Date: 2026-11-14

Status: planned, nothing built. Step 5 of
[nvidia-gt218-pgraph-2d.md](nvidia-gt218-pgraph-2d.md), after step 4's
HAL engine ran on M-BXMFA8
([2026-11-14-gt218-hal-engine-first-run.md](../decisions/2026-11-14-gt218-hal-engine-first-run.md)).
It has one decision that is Michael's before any code: who brings the
channel up for GDI.

## What is different from DirectDraw

GDI is drawn by the 16-bit driver (`src\display16\gdi_accel.c`), from
the first Enable, before any 32-bit code of ours is loaded. DirectDraw is
drawn by V9XHAL.DLL, which Windows loads only when a program first uses
DirectDraw.

Issuing an operation from 16-bit code is small, and everything it needs
exists:

- a push of at most 43 words, built by the push builders (pure C);
- written into the reserve's pushbuffer and IB ring through the
  framebuffer selector (`V9xNv3FbWrite`, by offset), and the kick and
  the idle reads through BAR0 (`V9xNv3Write` / `V9xNv3Read`, which the
  nvidia family already has for the RIVA 128);
- the IB index needs no shared variable: every operation waits for idle
  first, and then PFIFO's own IB get (3334h) is the next index. GDI and
  DirectDraw cannot interleave, because DirectDraw holds the Win16 lock.

**Bringing the channel up is the hard part.** It needs the channel's
structures (704 KiB, built by `nv50_chan.c`), the PGRAPH context program
and image (`nv50_grctx.c`, nouveau's generator, 126 KB of source) and the
register list. The RIVA 128's 16-bit side does its own small bring-up;
this one is not small, and `nv50_chan.c` writes through a flat pointer
the 16-bit driver does not have.

## The decision: who brings the channel up

**A. The HAL only; GDI uses the channel once it exists. (Recommended
first.)** The 16-bit side never initialises anything. Before each
operation it checks the two registers the HAL's guard checks (2604h and
40032Ch against the values for the reserve); if they hold, it draws on
the engine, and if not, on the CPU. Smallest change, no bring-up code in
the driver, and the channel is only ever built by the code that has run
on the card. The cost: until some program has used DirectDraw in a boot,
GDI stays on the CPU. Measured on M-BXMFA8, the channel then survives
mode sets, so it stays up for the rest of the boot.

**B. A, plus a start-up helper.** A small 32-bit program run at logon
creates and releases a DirectDraw object, so the HAL brings the channel
up as the desktop appears. GDI is then accelerated from logon on. One
more installed file and a Run entry; nothing modal (the rule about boot
UI applies).

**C. The 16-bit driver brings the channel up itself.** Always on, from
the first Enable. It needs the builders to write through a callback
instead of a pointer, and either the context generator linked into the
16-bit driver or a table of its output for this card's inputs (A8,
1540h = `F3010001`: 124 program words and 945 image words) generated at
build time and checked against the generator by a host test. The most
code and the most ways to differ from what was measured.

A is a subset of B, and nothing in A is wasted if C is wanted later.

## Is it worth doing

Not measured, and the plan should not pretend otherwise. A machine
with a GeForce 210 has a CPU that fills and copies through a
write-combined framebuffer quickly; the CPU path is what this card has
run since boot 83 with no complaint recorded. The engine's value here
is more in being exercised (the same channel and pushes will carry any
later work on this family) than in speed. Step 5.0 measures before
anything is built, and a result of "no visible difference" is a reason to
stop at A or not start.

## Steps

### 5.0 Measure the CPU path first

On M-BXMFA8 as it is now (boot 108's build): `V9XGDI /accel` and its
timings with GDI on the CPU, at 16 and 8 bpp, recorded as the baseline.
No comparison is chased (the project's rule on benchmarks); it is there
so 5.4 has a before.

### 5.1 The push builders on their own

Move the push builders (`v9x_nv50_push_*`, `v9x_nv50_ib_entry`,
`v9x_nv50_2d_format`) from `nv50_chan.c` to a new `nv50_2d.c`, so the
16-bit driver can link them without the channel builder and the context
generator. No behaviour change; the host tests move with them. The
manifest gains the object for the GT218 row.

### 5.2 The HAL keeps a channel that still holds

`eng_nv50.c` rebuilds the channel whenever an Enable has cleared its
validated flag (three times in the first-run session). With GDI drawing
on the same channel, the HAL instead re-validates without rebuilding
when its two-register guard holds, and builds only when it does not. A
mode set then costs nothing, as step 4.0 measured it need not.

### 5.3 The 16-bit arm

In `gdi_accel.c`, under the nvidia family: live when the row is the
GT218, BAR0's selector is up and the reserve is inside the mapping. At
gate 8, as the NV3 arm does: the guard, the idle wait, the push and IB entry, a write-combining
drain, the kick. Two sides now kick one ring, so neither can keep the
index in a variable of its own. After a kick, each side waits (bounded)
until PFIFO's copy of PUT (3330h) shows the new index; idle is then
3334h = 3330h with the pusher drained and PGRAPH idle, and the next
index is what 3334h reads. `eng_nv50.c` changes to the same rule in
5.2. A timeout or a latched interrupt poisons GDI acceleration for
the session, as on every other arm. Fill and copy only; text and upload
stay on the CPU.

Open point to settle in 5.3: the 16-bit driver has no write-combining
drain today. The HAL uses a locked instruction; the 16-bit side needs
the same before the kick, or the chip may fetch a push that is still in
the CPU's buffers.

### 5.4 On the card

Install as in step 4.4. `V9XGDI /accel` and `/auto`, V9XMSW cycles, and
V9XDDP again (the two sides now share one channel), with the GDI
counters (fills, copies, declines, timeouts) and the card's registers
read back as in the first-run record. The owner is asked to look at the
desktop: GDI is what he sees.

## Decisions owed

- A, B or C above.
- Whether 5.0's result justifies going past it.
