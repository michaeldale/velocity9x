# NFS II SE on Glide over Gen3 goes from 5 to 18 frames a second; a third buffer, not a wait, ends the flicker

Date: 2026-10-09
Machine: MICHAEL-NETBOOK (GMA 950, Gen3), boot 129.
Driver: 0.13.0 set as installed; GLIDE2X.DLL from the tree of this
record's commit, in the game directory.
Evidence: [`../probe/netbook-nfs2se-glide-frame-rate-2026-10-09/`](../probe/netbook-nfs2se-glide-frame-rate-2026-10-09/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 4.
Follows: [NFS II SE races on Glide](2026-10-09-nfs2se-races-on-glide-gen3.md).

## Why

The game raced with everything drawn at about 5 frames a second. The
DLL now times its own work with rdtsc, in buckets: the whole frame,
time inside exports, vertex preparation, texture upload, submission to
the render interface, swap and clear. It logs them every 15 s with
batch counts and why each batch ended.

## Measured

Swaps per 15 s in the race, from the probe folder's table:

| Stage | Swaps / 15 s | fps |
|---|---|---|
| Clear through the interface | 81 | 5.4 |
| Blitter fills | 173 | 11.5 |
| Plus CPU fixes | 269 | 17.9 |
| Drawn clear, two buffers | 262-270 | 17.5-18 |
| Drawn clear, three buffers (committed) | 278 | 18.5 |

1. **The clear was 53% of the frame** (12,785 of 24,056 Mc, run 1). The
   interface's clear writes 640x480 colour and depth with the CPU
   through the uncached aperture. Blitter colour and depth fills
   (DDBLT_COLORFILL / DDBLT_DEPTHFILL) cut it to nothing and doubled
   the rate. Then the game flickered.
2. **CPU work in the DLL.** The fog lookup became a binary search of a
   64-entry table of W values computed once. The clip pass copies a
   triangle that lies wholly inside the window. The state comparison
   that decides whether a batch continues compares 32-bit words. Prepare
   is now about 1,250 Mc of a frame.
3. **Batches are small, and the cause is textures.** Of about 330,000
   batches per 15 s, 99% end because the next triangle binds another
   texture ("batch ends: full=1599 texture=322867 state=0"), at about
   2.7 triangles a batch. Submission is 13,241 of 23,814 Mc, about
   40,000 cycles a batch, and is now the largest cost. Not changed: the
   remedies (a texture atlas in the DLL, or a cheaper per-batch path in
   the HAL) are larger pieces of work.
4. **The flicker followed the fills, not their order.** Each of these
   still flickered: a flush before the fill, a finish before it
   (run 5), waiting on GetBltStatus, the colour fill alone (run 6), the
   depth fill alone (run 8). Without fills (run 7) it did not, in game.
   The clear drawn as a quad (depth ALWAYS, depth written as
   depth/65535) cost 22 Mc and still flickered (run 9), so the cause was
   not the blitter either: it was the frame rate. At 5 fps drawing
   rarely overlaps the flip; at 18 it does.
5. **Drawing started before the flip reached the screen.** A two-buffer
   chain hands back the buffer being shown as the next target. Waiting
   on `GetFlipStatus(DDGFS_ISFLIPDONE)` before every write did not stop
   the flicker. A finish plus `WaitForVerticalBlank` after each flip did
   (run 10, operator: "looks good"), at 224 swaps, 15 fps. A third
   buffer (run 11) did too, with no wait, at 278 swaps. Operator: "a
   very small amount of flicker now in the menu ... the actual game
   looks good". The menu flicker was also there with no fills (run 7).

## Changed

- `glide_dll.c`: the profile and batch-end counters. The clear is drawn
  as a quad, falling back to the interface clear if the quad is
  refused. The word compare.
- `glide_vertex.c`: the fog table's binary search and the clip fast
  path. The host tests are unchanged and pass.
- `glide_surface.c`: one extra back buffer (`V9X_GLIDE_EXTRA_BUFFERS`).
  The bounded flip wait `v9x_glide_device_wait_flip` stays before every
  write; it is cheap (swap 16 Mc) and the lock path needs it. The
  texture-create failure log is capped at 8 lines.

## What the evidence disputes

- That the blitter fills or their ordering caused the flicker. A drawn
  clear flickered the same way.
- That `GetFlipStatus` saying the flip is done means the old front
  buffer is free to draw into on Gen3. It said so, and drawing into it
  still showed. Why is not established: the HAL's flip status, a
  latched-but-not-scanned flip, or a status that is per-surface and
  asked of the wrong one are all open.

## Open

- The pause and exit dialogs draw wrongly.
- The distant sky view without the HUD (previous record) is unexplained.
- Gamma is not applied.
- The menu's small flicker.

## Gates

- `run-checks.ps1` green.
- V9XGLIDP probe, three buffers: `Result=PASS failures=0` on the
  netbook, and exit 0 on the software-engine guest (86Box
  Win98SE-Fast-D3D).
- NFS II SE raced on the netbook, runs 1-11 above, ended with
  `WCLOSE.EXE`.
