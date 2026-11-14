# GT218 Tesla 3D: the object binds and takes EXA's setup without a fault

Date: 2026-11-14

Phase 1 of [docs/plans/nvidia-gt218-3d.md](../plans/nvidia-gt218-3d.md),
on M-BXMFA8 (GeForce 210, GT218 A8), boot 117. Evidence:
`build/driver-results/m-bxmfa8-gt218-3d/p1-V9XN5CH.INI` and
`p1-exec.txt`.

## What ran

`V9XN5CH /lean /3d`, the probe built from the code committed as 3c10c1b0
(the report says `ad574774-dirty`: it was built from the tree just
before that commit). On a fresh boot, before any DirectDraw use, the
probe built its own channel at video memory 128 MiB with the 3D object
in it (class 8597h at instance +E040h, RAMHT handle `BEEF8597h`), ran the
measured bring-up, and repeated the 2D stages (fill, copy, the three
depths), all of which passed as before. Then the `/3d` stage:

1. wrote EXA's vertex program (60 words) and `PFP_S` (6 words) 3 MiB into
   the window and read one word of each back;
2. kicked one 92-word push: the 3D object bound on subchannel 1, then
   xf86-video-nouveau's `NVAccelInitNV50TCL` setup without
   `DMA_NOTIFY`, as
   [the method list](../specifications/gt218-tesla-3d-first-triangle.md)
   gives it.

## Result: PASS

| Read | Value | Meaning |
| --- | --- | --- |
| `VpLastWord`, `FpLastWord` | `00000789`, `0000C785` | both programs in video memory as written |
| `GetWait`, `IdleWait` | 1, 0 | IB get reached PUT at once; PGRAPH idle |
| 3228h | `00002FFC` | no pusher error (bits 31:29) |
| 2100h, 400100h, 400108h | 0, 0, 0 | no PFIFO interrupt, no PGRAPH interrupt or trap |
| 400700h | 0 | PGRAPH idle |
| 40032Ch | `80008000` | PGRAPH holds the probe's channel |
| 400804h, 400808h, 40080Ch, 400814h | `80000000`, `00010FF8`, `20000000`, `00008597` | PGRAPH's last method: class 8597h, subchannel 1, method FF8h (`SCREEN_SCISSOR` vertical, the setup's last word), data `8192 << 16` |
| 100C90h | `050D0FFF` | no VM trap pending (bit 31 clear; 05h in 31:24 is the probe's own word select) |

The last-method words say the 3D object was bound and took every method
of the setup through to its last one. Earlier bring-ups leave the same
`80000000` status with no interrupt
([GDI record](2026-11-14-gt218-gdi-engine-first-run.md)), which is why it
is read as a record of the last method, not a fault; that reading is
still not measured.

## What this settles, and what it does not

- Class 8597h is the GT218's 3D class, and our context image accepts it:
  no microcode, no extra context state.
- EXA's setup is accepted without `DMA_NOTIFY`. Whether leaving it out
  matters for drawing is phase 2's question; nothing here notifies.
- Every address in the setup is inside the VM; none trapped.
- Nothing was drawn, so nothing here says the programs run, the
  texture tables are read, or a linear render target works. Those are
  phase 2.

The machine stays up on the merged nvidia build (2c0f02e8). The probe
left PGRAPH on its own channel; the HAL brings its channel back up at
the next DirectDraw use, as it did after the step 4.0 runs.
