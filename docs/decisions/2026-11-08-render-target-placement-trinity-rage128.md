# Offscreen render targets the 3D engine refused: Trinity at 16 bpp, Rage 128

Date: 2026-11-08

Status: **measured, two machines, one fix each.** Both faults found by the
0.41.0 release checks were the same kind of fault: the HAL placed an
offscreen render target where its own target check then refused it. On
Trinity it was the row pitch, on the Rage 128 the base offset. The
probes passing on the other machines did not show either.

- M-D7TFW4, HD 7660D Trinity (`1002:9901`), remote help, desktop 16 bpp.
  Boot 62 ran the 0.41.0 `ati` build (`a5f26008`); boot 63 runs the fix,
  built from `26a90ca5` plus the `d3d_eg.c` change (id `26a90ca5-dirty`).
  Reports:
  [`../probe/m-d7tfw4-trinity-16bpp-gl-pitch-2026-11-08/`](../probe/m-d7tfw4-trinity-16bpp-gl-pitch-2026-11-08/).
- A8U4I5, Rage 128 Pro (`1002:5046`), desktop 32 bpp. Boot 679 ran
  `a5f26008`; boot 680 runs both fixes (id `26a90ca5-dirty` again, a
  later build). Reports:
  [`../probe/a8u4i5-rage128-t4-target-align-2026-11-08/`](../probe/a8u4i5-rage128-t4-target-align-2026-11-08/).

Every file of each build was installed by WININIT rename and its SHA-256
compared after the restart: 8 of 8 on both machines.

## Trinity: OpenGL reads wrong at 16 bpp

The 0.41.0 checks found every V9XGLP read white from the SGIS mip
sweep on ([record](2026-11-08-0.41.0-release-checks.md)). What comes
just before the sweep is the lifetime test, which resizes the probe's
window from 400 to 360 pixels wide at cycle 25.

The probe gained three things for this (`gl_probe_win32.c`):

- `LifetimeClearAfter`, which clears to blue right after the lifetime
  cycles and reads the pixel back;
- `/noresize`, which skips the resize;
- `/resizew:N`, which resizes to N pixels wide instead of 360.

| Run (boot 62) | Client width | `LifetimeClearAfter` | `OffsetNone` |
|---|---|---|---|
| default | 360 | `FFFFFF` | `FFFFFF` |
| `/noresize` | 400 | `0000FF` | `FF0000` |
| `/resizew:344` | 344 | `FFFFFF` | `FFFFFF` |
| `/resizew:352` | 352 | `0000FF` | `FF0000` |
| `/resizew:368` | 368 | `0000FF` | `FF0000` |
| `/resizew:376` | 376 | `FFFFFF` | `FFFFFF` |
| `/resizew:384` | 384 | `0000FF` | `FF0000` |

Each width that fails is an odd number of 8-pixel units. At 16 bpp its row
is 16 bytes past a multiple of 32. The ICD's log for the 376 run shows
the drawable made at 376x280 without error, then `glClear result=1` for
every clear after it: the HAL refused the target.

The cause is a mismatch inside the HAL:

- `v9x_d3d_eg_create_surface` placed an offscreen render target with its
  pitch rounded to 8 pixels;
- the core's target check takes `target_pitch_align`, which the
  Evergreen engine sets to 32 bytes ("8 pixels at 32 bpp").

At 32 bpp the two agree; at 16 bpp, 8 pixels are 16 bytes. Cedar at
32 bpp passed this probe for that reason. The placement now also rounds
the pitch up to `target_pitch_align` bytes.

Boot 63, with the fix:

- V9XGLP at the default 360 width: `Result=PASS`,
  `LifetimeClearAfter=0000FF`, `CvaResult=PASS`, and `Offset*Ok`,
  `Fog*Ok`, `ReplaceRgb*Ok` and `AttribClearOk` all 1. `TnlResult` now
  reads `match`.
- V9XD32 at 32 bpp and at `/bpp16`: `Result=PASS`, no failures.

`LitResult=differ` remains. It is the probe's label for an engine without
hardware T&L (every lighting sample reads unlit white), the same on the
R3E, the GMA 950 and the Rage 128.

## Rage 128: T4 and T16 failed after T20 and T21

V9XD32's T4 and T16 had failed on this card since the probe gained T20
and T21 in 0.37.0. The probe was blamed
([issue](../issues/2026-11-06-v9xd32-t20-t21-break-t4-t16-on-rage128.md)).
It was the driver.

T4 creates a 64x64 offscreen video-memory target. Its address and the
back buffer's (`BackSurface`, at `D3dTargetOffset=12C000h` in the
V9XTRACE snapshot) give its offset in video memory:

| Run | `T4ClearedSurface` | Offset | Base on 32 bytes | T4 |
|---|---|---|---|---|
| 0.36.0 probe, boot 671 | `CC7A85A0` | `2EE5A0h` | yes | EXACT |
| main probe, boot 679 | `CC7A926C` | `2EF26Ch` | no, `+0Ch` | FAIL |
| main probe, boot 680 (fix) | `CC7A9280` | `2EF280h` | yes | EXACT |

T20's and T21's textures are placed by the HAL itself, in blocks from
DirectDraw's heap (`d3d_place.c`). Since then, the next heap allocation,
T4's target, starts at an offset only 4-byte aligned. The Rage 128's 3D
engine takes a target on a 32-byte base (`V9X_D3D_R128_SURFACE_ALIGN`,
`V9X_R128_OFFSET_ALIGN`). The fill came back `DDERR_UNSUPPORTED`
(`0x80004001`, read before as E_NOTIMPL) and the draw left nothing.
Which check refused the fill was not traced: the 2D fill code biases an
unaligned base by whole pixels and should take it. The Evergreen engine already placed offscreen render
targets itself for the same reason (V9XD32's T4 on Trinity, boot 18). The
Rage 128 placement now does the same: a single offscreen 3D target gets a
32-byte base and a pitch of whole 8-pixel units.

Boot 680:

- V9XD32: `Result=PASS`, no failures, at the default (DDI 5), `/ddi6`
  (`RunDdi=6`) and `/ddi5`. T4Flat EXACT and T16 PASS in all three runs.
- Against the boot 679 report, nothing changed but T4, T16 and addresses.
- V9XGLP: `Result=PASS`. Every verdict and pixel matches the 0.41.0
  check's, apart from build and timing lines. The ICD's back buffer is
  one of these targets, so it now goes through this placement.
- V9XGLIDP: `PASS failures=0`. V9XR3DP: `COMPLETE`.

Any application on the Rage 128 could reach this state: create textures,
then an offscreen render target.

## Not changed, not measured

- **The ICD drops a refused clear or draw.** On Trinity, every refused
  clear was logged and skipped, with no software fallback and no GL
  error. A refused target now shows only in `V9XGL.LOG`.
- **Other engines.** Only Evergreen and Rage 128 were checked for this
  mismatch. The VI and GCN placement already rounds rows to its alignment
  (`d3d_vi.c`). The rest were not audited.
- **Machines.** Trinity ran the build with the `d3d_eg.c` change only,
  not the later build with both changes. Cedar, Juniper, Redwood and
  Caicos share `d3d_eg.c` and did not run it. No game was run on either
  machine.
