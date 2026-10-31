# The owner's HD 5450 reports on the HD 5770: one bug found and fixed, one gone

Date: 2026-11-01
Status: M-BXMFA8's HD 5770 (Juniper), boots 49 and 50. The owner's list
(`Issues.txt` on their desktop, written against the HD 5450) re-run item
by item. **Quake 3 at 1280x1024 and above was a DMA copy declined to the
CPU: fixed, 1.3 to 50.6 fps.** The resolution-change corruption does not
reproduce. The 3DMark 2000 trees and 3DMark 2001 plants are not settled.
Captures: [`docs/probe/m-bxmfa8-juniper-reports-2026-11-01/`](../probe/m-bxmfa8-juniper-reports-2026-11-01/).
The owner's own screenshots were viewed but not copied into the repo.

## Quake 3: 1-2 fps at 1280x1024 and above

**Reproduced** on boot 49 (`d229c333`): the demo at 1280x1024, 32-bit,
`timedemo 1`, `demo001`: **1346 frames, 1020.0 s, 1.3 fps**, against 49.3
at 1024x768x32. The ICD drew every batch on the engine (214,404 batches,
cpu 0, refused 0) and evicted no textures, so the drawing was not the
cause.

**Cause.** A whole 1280x1024x32 surface is 5,242,880 contiguous bytes,
more than one DMA packet's `0FFFFFh` dwords (4 MiB). `v9x_eg_dma_plan_fill`
already falls back to a packet a row past that; `v9x_eg_dma_plan_copy`
did not, and declined the copy, which DirectDraw then did on the CPU,
reading VRAM over the bus. The ICD presents with a full-surface blit
(31 flips in the whole 1024x768 run), so every frame paid that read. At
1024x768x32 the surface is 3 MiB and fits one packet, which is why it was
fast. 3DMark 2000 at 1600x1200 was fine for the owner because Direct3D
presents by flipping.

**Fix.** A contiguous copy is one packet only while it fits; past that
the rows go one a packet, with the existing bottom-up order for an
overlap. `test_copy_past_one_packet` (`test_eg_dma.c`) was written first
and failed on exactly the oversize cases. `run-checks.ps1` passed.

**Boot 50**, the set rebuilt with the fix and checked by hash:

| Quake 3 demo, 32-bit colour | Before (boot 49) | After (boot 50) |
|---|---|---|
| 1280x1024 | 1.3 fps (1020 s) | **50.6 fps** (26.6 s) |
| 1600x1200 | not finished (stopped after 8 min) | **49.0 fps** (27.5 s), 215,597 batches on the engine |

The fix is in the shared Evergreen code, so Cedar and Caicos take it
too; neither was run at these sizes.

## Quake 3: corruption after an in-game resolution change at 32 bpp

**Not reproduced.** Desktop set to 1024x768x32; Quake 3 started at
640x480 (`r_mode 3`), then `r_mode 4; vid_restart` to 800x600, left at
its menu. While the menu was up, V9XEGMM read D1: graphics enabled,
32 bpp, pitch 800 pixels (3200 bytes), viewport 800x600, surface at
offset 0, matching the driver's own `Surface=pitch=3200 bpp=32 ...
w=800 h=600`. The agent's screenshot (the primary at offset 0, which is
what is scanned) shows the menu drawn correctly. The owner's picture,
taken on the HD 5450 before flips and the 128 MiB heap, shows a 640x480
frame read at the wrong pitch and depth; nothing of that kind is present
now. Which change removed it was not established.

## 3DMark 2000: Helicopter trees vanish with Pentium III optimisation

**Not settled.** Two hypotheses tested and dead:

- *The GPU clipping pre-transformed vertices whose z falls outside 0-1
  (3DMark's own transform, unlike Direct3D's, does not clip z first).*
  Our default context writes `PA_CL_CLIP_CNTL` `00010000`, `CLIP_DISABLE`,
  as Linux 3.9's does (`eg_3d.c`, `evergreen_blit_shaders.c`). The
  clipper is off.
- *Draws refused by the Evergreen engine going to the CPU fallback and
  landing out of order.* The default benchmark (Pentium III
  optimisations, 1024x768x16, triple buffer) completed, score 4037
  (recorded, not compared), and the trace afterwards shows **zero**
  refusals in every reason (`M64Policy00`-`19`), no texture refusals,
  17,972 flips with none declined.

What remains is visible only on screen. The agent's screenshot cannot
see fullscreen 3D; V9XTRACE's frame grab could catch Helicopter frames
and was not tried.

## 3DMark 2001: resolution lists at 32 bpp

**Partly addressed, by the 2026-10-29 mode-subset change.** V9XDDP
`/pal8` on the 16 bpp desktop: DirectDraw enumerates its full 32 modes,
six of them at 32 bpp: 320x200, 320x240, 512x384, 640x480, 800x600,
1024x768. The owner saw only 640x480. Nothing above 1024x768 is offered at
32 bpp, because the 32 slots are full; that is the mode table's limit.
The 32 bpp-desktop case was not re-run.

## Not re-run

- 3DMark 2001 plants drawn opaque in the Lobby test (the owner's
  screenshot shows black blocks where the plants' transparent texels
  should be). Needs the Lobby test's frames.
- 3DMark 2000 flicker with single or double buffering.
