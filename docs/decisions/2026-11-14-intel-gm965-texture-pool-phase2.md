# Intel GM965 texture pool, Phase 2: the pool maps and the aperture reaches its pages; one earlier run froze the machine for 6.5 hours, unexplained

Date: 2026-11-14
Machine: TOMMOHP (`M-KYHT4Q`, Tom Rossell's HP laptop, GM965 GMA X3100
`8086:2A02`), through remote help, boot 55, on the driver the 0.50.0
release checks left (16 MiB of GMADR mapped and write-combined; `[IntelPool]
Result=not-gated-device`).
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, Phase 2, on the
first Gen4 chip. Phase 0: `2026-11-13-intel-gm965-texture-pool-phase0.md`.
The Gen3 record it repeats: `2026-11-12-intel-gen3-texture-pool-phase2.md`.
The freeze: `../issues/2026-11-14-gm965-pool-map-probe-machine-stops.md`.
Evidence: `docs/probe/gm965-texture-pool-phase2-2026-11-14/`.

The first GTT writes this project has made on a Gen4 chip. No GPU
register, ring or command was touched.

## The tool

`G3PM.VXD` + `V9XG3PM.EXE` (`tools/diag/gen3_pool_map*.{asm,c}`), changed
twice:

- **For the GM965:** `2A02` in the device list, the GTT base passed as
  GTTMMADR + 512 KiB.
- **After the freeze below:** staged, small and timed. Arguments
  `cycles pages stage first`: `pages` maps that many instead of the
  plan's; `stage` stops MAP after the PTEs (1), after mapping the aperture
  range untouched (2), after one dword through it (3), or runs the whole
  pattern (4); `first` moves a small run to another entry. Every pattern
  loop reads the time-stamp counter and stops itself after 2 to 4 times
  2^32 cycles (`TimedOut`), and the PTE writes, their read-back and the
  two pattern passes report cycles.

The changed probe ran all four stages on the netbook first (4 pages at
entry `3000`, clear of the pool its driver has mapped): PASS each.

## Result: PASS, at every size up to the plan's

On TOMMOHP, at the plan's entries (`800` up), each run restoring every
entry to `7FF7E001` and freeing its pages:

| Run | Result |
|---|---|
| stage 1, 4 pages | PTEs written and read back, 0 mismatches |
| stage 2, 4 pages | and the aperture range mapped |
| stage 3, 4 pages | one dword through the aperture read back through it and through the page's own mapping |
| stage 4, 4 / 256 / 2,048 / 2,304 / 4,096 / 6,144 pages | pattern exact through the aperture and in RAM, 0 mismatches each |
| stage 4, 8,192 pages (the plan) | write 196 ms, read back through the aperture 1,248 ms, 0 mismatches either way |
| the full run: 8,192 pages and two 640x480 mode cycles | PASS: both cycles succeeded, PTEs and pattern 0 mismatches after them |

No run timed out. Writing 8,192 PTEs took about 0.5 million cycles and
reading them back 1.7 million. The pattern write slows past 2,048 pages
(6 ms for 8 MiB, 14 ms for 9 MiB): the driver's write-combining MTRR ends
at GMADR 16 MiB, so the pool's upper 24 MiB is written uncached on this
build. A driver with the pool has to map and write-combine 64 MiB, as the
GMA 950's row does (Phase 0 showed the DPMI host maps it here).

So on this GM965: PTEs written at GTTMMADR + 512 KiB stick; a CPU store
through GMADR above the stolen run reaches the page the PTE names; and a
VBIOS mode set, through the 1280x800 panel patch, leaves those PTEs alone.

## The freeze, not explained

The first run of the GM965-extended probe, before it was staged, at
02:33:36 on the same boot: defaults, the same 8,192 pages (frames
`044BB000`-`17C35000`, as the passing full run got). The machine's own
agent log: `exec-start` at 02:33:36, `exec-complete` at 09:02:45 with exit
`FFFFFFFD` after 23,352 s, and a poll gap of 23,353 s. The whole system
stood still for 6.5 hours, inside MAP (`Step=map`; the status on disk is
ALLOC's). It then went on without a restart. A read-only `G3PL` run
afterwards found entries `7E0`-`27FF` all `7FF7E001`, hash `90EB6205` as
before any write, and the free-page count back.

Seven hours later the same pages through the same steps pass in 1.5 s. What
differed is not established. Not tested:

- The laptop had had no input for 76 minutes (the last injected key was
  at 01:17:41) and was unattended at night; it came back at 09:02, when
  its owner may have touched it. A power-management state (standby, or
  the panel or graphics device powered down) would stop the timers the
  same way. `CurrentPowerPolicy` reads 3; its timeouts were not read.
- A fault screen that waited for a key would also look like this.
- The staged probe differs from the frozen one only by its time-stamp
  reads, timings and stage tests; none of them was reached.

## What this means for a GM965 pool

The mechanics the Gen3 driver path uses work on this chip. Before the
driver maps a pool here at every boot, the freeze needs either a cause or
a reproduction attempt under the watchdog: the staged probe, full size,
run after the machine has sat without input for 80 minutes or more. If
the aperture is unreachable in some idle state, a pool that textures are
uploaded into is exposed to it and stolen memory may be too.

Still owed from the Phase 0 record: Gen4 PTE address bits 35:32 (every
frame seen was below 4 GiB), the Gen4 HAL's texture binding from the
pool, and a decisions record for this chip.

## State left behind

TOMMOHP: boot 55, the 0.50.0 checks' driver, GTT as the BIOS left it.
`G3PM.VXD`, `V9XG3PM.EXE` and Phase 0's pair are in
`C:\V9XREMOTE\JOBS\G3PL`. The netbook: unchanged.
