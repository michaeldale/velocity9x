# Need for Speed II SE races on GLIDE2X.DLL over Gen3, everything drawn, at about 5 frames a second

Date: 2026-10-09
Machine: MICHAEL-NETBOOK (GMA 950, Gen3), boots 127-129.
Driver: 0.13.0 set as installed; GLIDE2X.DLL from the tree of this
record's commit, in the game directory.
Evidence: [`../probe/netbook-nfs2se-glide-race-2026-10-09/`](../probe/netbook-nfs2se-glide-race-2026-10-09/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 4.

## Why

Phases 2 and 3 passed on the probe. This is the first game run with
drawing.

## What the runs found, in order

1. **Live textures were evicted.** With the Phase 3 DLL the main menu's
   left panel was black. Once the attract-mode race started, 52,204
   textured draws were skipped. The TMU table held 512 records, but 2 MiB
   holds 1,024 of the 32x32 ARGB1555 textures NFS II SE uses most, so
   live ones were evicted. It now holds 4,096 records, with a hashed
   lookup by address because `grTexSource` runs tens of thousands of
   times a second. A host test of 1,024 live textures fails at 512 and
   passes at 4,096. The menu, the Player One screen and the race then
   drew completely.
2. **Gen3 refused one state family** (35,670 triangles in about 75 s),
   and 87,091 triangles were redrawn without fog. The logged state was an
   ARGB1555 surface, MODULATE, (SRC_ALPHA, INV_SRC_ALPHA), alpha test
   GREATER 16, depth LEQUAL without writes, and a scissor of the HUD pane
   537..634. Gen3's builder refuses any scissor short of the whole target
   (`d3d_i9xx.c`, explicit draws), as do the other hardware engines.
   The clip window is now applied to the geometry in screen space
   (`v9x_glide_clip_rect`, host-tested, as the ICD applies GL's scissor)
   and draws carry the whole target. After that: **0 refused, 0
   fog-dropped**, the rear-view mirror and the track map drawn.
3. **The first clipping cut only half of each line.** The clip function
   wrote the whole-target scissor back into the setup it was given, so a
   line's second triangle went unclipped and the map's route spilled out
   of its pane. It now works on a copy.
4. **Video memory runs out.** Phase 3 refilled a texture surface for every
   re-download, about 300 a second. A content-keyed surface cache, with
   each palette identified by a checksum of its contents, cut uploads in
   a race to a handful. The first cache then filled video memory
   (`DDERR_OUTOFVIDEOMEMORY` creating a 64x64 surface) with the menu's
   animated 256x256 frames. It now evicts the least recently bound
   surface and retries.

## Where it stands

- The menus, car selection and a race (the default track at night, rain,
  an aircraft overhead, rear-view mirror, track map, HUD) draw through
  Glide on the GMA 950.
- Per 15 s in the race: about 80 swaps (**about 5 frames a second**) and
  about 270,000 triangles drawn, with 0 refused, 0 fog-dropped and 0
  skipped.
- Uploads are flat once the race has loaded.

The frame rate is recorded, not compared. The uploads were not the cost.
Where the time goes is the next measurement, not yet made.

## Not explained or not done

- The game's own pause and exit dialogs draw wrongly. Michael saw it on
  the netbook (the in-race Escape menu); the main menu's exit prompt read
  back black. Not investigated.
- A screenshot 10 s into one race showed a distant sky view without the
  HUD (`B129-RACE-LATER.png`). Not explained.
- Keys injected by the agent are often missed at 5 frames a second, and
  Escape does not open the in-race menu that way.

## Gates

`build-host.ps1` (each new test seen failing first), `check-tree.ps1`,
`run-checks.ps1`; V9XGLIDP passing on the netbook with the final DLL.
