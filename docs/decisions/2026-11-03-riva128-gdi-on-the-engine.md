# RIVA 128ZX: GDI fill and copy on the NV3 engine, at 8 and 32 bpp

Date: 2026-11-03

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition. Plan: `docs/plans/nvidia-riva128.md`, Phase 6.
Files: `docs/probe/m7v636c-riva128-gdi-2026-11-03/` (`Build=` in them is
V9XGDI's own build, 37d04471; the driver is named by hash below).

## What was built

`gdi_accel.c` has an NV3 arm under `V9X_NVIDIA_FAMILY`, the shape of the
MGA and 3dfx arms: gate 8 builds the fill or copy with `nv3_2d.c`, the
builder eng_nv3.c uses, and the arm writes what it returns.

Two things differ from those arms:

- **Reach.** NV3's registers run from PMC at 0 to the USER area at
  800000h, and the 16-bit side's engine selector has a WORD limit and WORD
  offsets. `runtime.asm` (nvidia family only) gains `V9xNv3Selector`, the
  same selector described over all 16 MiB of BAR0, and 32-bit-offset reads
  and writes through it and through the framebuffer selector, as the Intel
  GMA family reaches GMADR.
- **Bring-up.** GDI draws from boot, before any DirectDraw program has had
  the HAL bring the engine up, and every mode set wipes RAMIN. So the GDI
  side runs the same bring-up from the same builder on its first operation
  after each Enable, and again whenever RAMIN's mark or PFB_CONFIG_0 is not
  what it wrote. That is the HAL's test, and both sides write identical
  values, so either bring-up satisfies the other. RAMIN is written through
  the framebuffer selector at the reserve hook's offset, in the measured
  reversed layout.

Idle is rivafb's nv3Busy, as in the HAL. GDI and the HAL submit to the same
channel with no lock between them, which is also true of the MGA and
VSA-100 arms.

At 16 bpp the engine sets bit 15 of every pixel, red's top bit at 5:6:5
(`docs/decisions/2026-11-02-riva128-2d-engine-writes.md`), so the arm
declines there. The first build declined per operation at gate 8; V9XGDI
`/accel` then failed the run with `fill-enabled-but-never-fired`, its check
for a primitive that is enabled but never fires (pixels compared PASS). The
second build clears the mode's primitives in `v9x_gdi_accel_configure`, which
every Enable runs, so 16 bpp reports no acceleration instead.

## Boot 17 (first build, DRV `D522951473408E92`)

V9XGDI `/accel`, 500 operations, 20 comparisons, at 800x600:

| Depth | Fills | Copies | Compared | Result | IdleTimeouts | Poisoned |
|---|---|---|---|---|---|---|
| 16 | 0 | 0 | PASS | FAIL (never fired) | 0 | 0 |
| 32 | 165 | 131 | PASS | PASS | 0 | 0 |
| 8 | 408 | 305 | PASS | PASS | 0 | 0 |

Counters are cumulative for the boot. The copies include V9XGDI's 41
overlapping ones per run; the engine picks the direction itself, as the
Phase 3 probe measured.

## Boot 18 (second build, DRV `AEB3DB7AA921DF77`)

| Run | Enabled | Fills | Copies | Compared | Result |
|---|---|---|---|---|---|
| 16 bpp | 0 | 0 | 0 | PASS | PASS (all `DeclineDisabled`) |
| 32 bpp, after the switch from 16 | 7 | 165 | 131 | PASS | PASS |
| V9XDDP from a 32 bpp desktop | | | | | `COMPLETE`, `BltFillPixelOk=1`, `Flip20Ms=323` |
| 32 bpp, after V9XDDP | | 416 | 305 | PASS | PASS |

The ReEnable from 16 to 32 enables the primitives again, and GDI and the
HAL each found the engine usable after the other had run. No idle timeout
and no poison in any run.

Not seen: the monitor, and real applications. V9XGDI compares its own
readbacks; it does not show what a desktop of overlapping windows looks
like after an hour.
