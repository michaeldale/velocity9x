# GT218: the NV50 2D engine in the HAL (DirectDraw fill and copy)

Date: 2026-11-14

Status: planned, nothing built. Step 4 of
[nvidia-gt218-pgraph-2d.md](nvidia-gt218-pgraph-2d.md), whose step 3
passed on M-BXMFA8 on 2026-11-14
([the record](../decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md)):
the driver's own channel, VM, context program and 502D methods filled a
64x64 rectangle, read back exact.

## What is measured, and what the engine may rely on

On M-BXMFA8 (GT218 A8, 512 MiB, BAR1 linear, boot 106), with the probe
`V9XN5CH /flush:none /push:class02`:

- nouveau's `nv50_fifo_init` and `nv50_gr_init` sequences, the context
  program upload and one channel bound at BAR1 + 128 MiB survive and
  work; PMC_INTR_EN stays 0 and nothing is taken as an interrupt.
- A kick is one write, IB PUT at BAR0 C0208Ch. Progress is read from
  PFIFO's own registers: IB get 3334h, DMA get 3244h and put 3240h,
  state 3228h (error in bits 31:29), PFIFO_INTR 2100h, PGRAPH_INTR
  400100h, PGRAPH idle 400700h = 0. **USERD is never read** (runs 3
  and 6 lost the card that way).
- A 32 bpp X8R8G8B8 solid fill through class 502D.

Not measured, and so not to be assumed by the engine until it is:

- a 502D **copy** (the builder has no copy push yet);
- fills at **16 bpp and 8 bpp** (502D destination formats other than
  X8R8G8B8);
- whether a **VBE mode set** leaves PFIFO, PGRAPH and the channel as the
  engine left them (the RIVA 128's did not);
- the push DMA object in class 3Dh, the builder's default (every passing
  run used 02h);
- more than three IB entries, and the IB ring wrapping;
- the engine region anywhere but BAR1 + 128 MiB.

## Shape

It follows the RIVA 128's engine, the family's existing pattern
(`engines\eng_nv3.c`, `riva128_hw16.c`):

- **16-bit side** (`gt218_hw16.c`): an engine descriptor that has the
  mini-VDD map BAR0 (16 MiB) and reports a new engine type,
  `V9X_DD_ENGINE_TYPE_NV50` (15), with SOLID_FILL and SCREEN_COPY; a
  reserve hook that takes the engine's region from the top of the
  128 MiB BAR1 mapping. The region is B0000h bytes today (instance
  block, page table, runlist, IB ring, pushbuffer, context image),
  rounded to 1 MiB and 64 KiB aligned, so the DirectDraw heap loses
  1 MiB of 128. This settles the plan's first owed decision for now: top
  of the mapping, no second mapping.
- **Policy in host-tested C** (`src\chipsets\nvidia\gt218\`): the
  register bring-up as an ordered list the HAL replays (today it lives
  only in the probe EXE), the copy push, the per-depth destination
  format, and the IB ring's index arithmetic. `nv50_chan.c` and
  `nv50_grctx.c` are used as they are.
- **HAL** (`engines\eng_nv50.c`): ready, validate, validated, can_blt,
  wait_idle, fill, copy, as `V9X_ENGINE32_OPS`. It writes the region
  through the framebuffer mapping and registers through BAR0.

The engine's rules, each from a measurement above or from eng_nv3.c's
history:

- One operation per kick, then wait: IB get = put, DMA get = put,
  PGRAPH idle, all bounded. After the wait the pushbuffer is reused from
  its start, so only the IB index advances and wraps.
- An error (PFIFO_INTR or PGRAPH_INTR non-zero, a pusher error, a
  timeout) gives the engine up for the session and declines to the CPU.
  No recovery path, as on the RIVA 128.
- A depth the engine has not been measured at is "present and idle":
  validate and wait answer yes without touching a register, fill and
  copy decline (the hang on M-7V636C,
  `docs\issues\2026-11-02-riva128-hang-under-v9xddp-at-32bpp.md`).
- The channel's VM is the identity over the first 512 MiB, and BAR1 is
  linear, so a surface's VM address is its heap offset. Nothing is
  mapped per surface.
- The push DMA object keeps the builder's class 3Dh: run 36 measured it
  working, and 02h equally (4.0 item 6).
- An overlapping copy is passed to the engine as it is, and 32, 16
  (R5G6B5) and 8 bpp are all drawn (4.0 items 2 and 3).
- Statics are shared by every process (V9XHAL), which is what the
  engine's state is.

## Steps

Each ends in a gate or a measurement recorded in docs\decisions\. The
card is on a remote-help machine: its owner is told before a run that
could stop it, and asked through M-JJN1ZZ if it does.

### 4.0 Measure what the engine needs, with the probe

New probe stages on M-BXMFA8, in this order, each stopping the run on
the first failure:

1. **Copy**: a 502D blit of the filled rectangle to another place on
   the test surface, non-overlapping, then overlapping in each
   direction; read back. The copy push is written and host-tested first
   (4.1).
2. **Depths**: the fill and copy at 16 bpp (R5G6B5) and 8 bpp, on test
   surfaces of those formats. The desktop's depth does not matter to
   the probe.
3. **Many kicks**: a few hundred small fills, one per kick, to carry the
   IB ring past its 512 entries.
4. **Mode set**: with the channel up, have the desktop change mode
   (and back), then read 2604h, 32ECh, 40032Ch, 3334h and the region's
   first words, and try a fill. This decides whether the engine keeps a
   state check and rebuild, as eng_nv3.c does.
5. **Class 3Dh** for the push object, once: if it works the default
   need not change; if it does not, 02h is the default and the record
   says why.

### 4.1 The builder's missing pieces, host-tested

`nv50_chan.c` / a new `nv50_2d.c`: the copy push (cl502d.h's blit
methods), destination and source formats per depth, the bring-up
register list with its bounded waits, the IB ring index. Tests against
the encodings, and against the trace where nouveau's value is known.
The probe is changed to take the bring-up from this module, so the list
the HAL replays is the list that was measured.

### 4.2 The 16-bit row

`V9X_DD_ENGINE_TYPE_NV50` in engine_abi.h; `gt218_hw16.c` gains the
engine descriptor and the reserve hook; the mini-VDD's BAR0 map service
is reused (it takes a base and a size; whether its NV3 name needs
generalising is a small design point to settle there). `d3d_select.c`
gives the type no Direct3D. check-tree, build-host and run-checks.

### 4.3 The HAL engine

`engines\eng_nv50.c` and its line in `ddhal_core.c`. Bring-up on first
validate; the state check from 4.0's mode-set result. run-checks.

### 4.4 On the card

Install on M-BXMFA8 by the WININIT route, with the vbe-tier files
backed up as they are today. Then, recorded with counters:

- V9XDDP's DirectDraw keys before and after (the plan's original
  measure for this step);
- V9XGDI /auto and V9XMSW, which must still pass (GDI stays on the
  CPU until step 5);
- fills and copies at 32, 16 and 8 bpp, with the engine's fill, copy,
  decline and timeout counters read back.

## Status

2026-11-14: **4.0 is complete**, every item measured on M-BXMFA8
([the record](../decisions/2026-11-14-gt218-502d-engine-measurements.md)).
4.1 is next.

## Decisions owed

- Settled by 4.0: a mode set does not disturb the channel, so no rebuild
  on a mode change. The engine keeps a two-register guard (2604h and
  40032Ch against what it wrote) before an operation and gives the
  engine up if it fails; a full-screen DOS box, standby and a driver
  restart are untested.
- Whether the mini-VDD BAR0 map service is renamed for two NVIDIA
  generations or a second one is added.
- When to try the other differences from nouveau's trace (RAMFC 7Ch
  `30000001`, 100C00h bit 2): not needed for anything measured so far,
  so left alone unless 4.0 fails somewhere they could explain.
- Step 5 (GDI on the engine) is planned separately once 4.4 passes.
