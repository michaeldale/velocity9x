# GeForce 210 after cube maps, signed formats and mipmaps: 3DMark 2000 and 2001 SE run to their scores with nothing refused; the partly drawn frames go with vsync on

Date: 2026-11-25. Machine: W9QI0DAD (AMD FX-6300, Windows 98 SE), ASUS
GeForce 210, GT218 `10DE:0A65`, `nvidia` package from `0fc5a43f` (cubes,
signed formats, mipmaps), desktop 1024x768x16, boot 149. SYSTEM.INI has
`VSync=2`, left by Michael from the Oland's runs, so every flip goes out
unsynced (`FlipVSyncOverridden` equals `FlipHandled`). Runs driven by
`run-3dm.ps1`: no frame grabs, V9XTRACE snapshots before and after, a
`-beat` every ~20 s for a freeze. HDMI frames taken through OBS. Scores
are recorded, not compared.

## Results

| Run | Result | Score | Time |
|---|---|---|---|
| 3DMark 2000 default | ran to its score | 5312 | 5.9 min |
| 3DMark 2001 SE default | ran to its score | 2966 | 7.4 min |
| 3DMark 2001 SE, `VSync=1` | ran to its score | 2362 | 7.3 min |

Counters across each run:
- **Engine:** no timeouts, resets or faults in either run.
- **3DMark 2000:** no draw refused.
- **3DMark 2001 SE:** the only change is `M64Policy19` 0 to 160. That is
  `V9X_D3D_NV50_DROP_GUARD`: single triangles dropped as past the guard
  band or not finite, the same 160 as earlier records on this card.
- The colour-key (8) and texture-op (11) counts were there before
  3DMark started; the probes made them.
- **3DMark 2000** added 6,514 untextured batches (`DrawsNoHandle`,
  ordinary geometry) and one two-unit ValidateDevice refusal, an answer
  to 3DMark's question, not a draw.

## On screen

- **3DMark 2000** (3 frames, the second game test):
  - two frames whole and plausible, the distant cobblestones smooth, as
    mip levels would make them;
  - the first frame drawn in its top 485 rows only, the rest black.
- **3DMark 2001 SE** (9 frames):
  - seven whole, Nature among them (trees, grass, water and lens flare
    drawn);
  - one all black;
  - one with only a strip at the top drawn.

The README's "parts of its frames not drawn" for this card holds in that
form: the odd frame, not every frame. With every flip unsynced
(VSync=2), a buffer can reach the screen while it is still being drawn,
which is how these frames look. Michael's own setting is vsync on, and
VSync=2 runs are diagnostic only.

## Rerun with vsync on

`VSync=1` set in SYSTEM.INI (read on each DirectDraw session, so no
restart), 2001 SE run again the same way, then `VSync=2` put back
(file hash checked).

- **Flips:** `FlipVSyncOverridden` stayed at 69,021 across the run (no
  new overrides) and `BltFlipWaited` went 0 to 8,898; V9XHW.INI reports
  `VSync=1`. The fps readouts sit at 60, 30, 20, 15 and below, the
  steps a synced flip gives at 60 Hz.
- **Counters:** no engine timeouts, faults or draws refused;
  `M64Policy19` 160 to 320, the same 160 guard-band drops as the first
  run.
- **On screen** (39 OBS frames, every game test and the start of the
  feature tests): every in-test frame whole. Two exceptions, neither a
  part-draw: one all-black frame between tests (the mode change into the
  loading screen) and one Nature frame washed almost white with the HUD
  intact, the scene's sun glare, which the next frames show fading.

So the partly drawn frames of the first run go with vsync on: they were
unsynced flips showing a buffer still being drawn, as suspected. The
first run's all-black frame may also have been a transition; its
timing was not recorded.

## Not shown

- Which game tests cube maps, signed formats or mip levels reached (no
  per-feature counters).
- How full the texture pool got.

Evidence: [`../probe/gt218-3dmark-w9qi0dad-2026-11-25/`](../probe/gt218-3dmark-w9qi0dad-2026-11-25/)
(each run's log, snapshots, score window and end screenshot, the vsync-on
run in `3dmark2001se-vsync-on/`; `obs/` and `obs-vsync-on/` the HDMI
frames; `run-3dm.ps1`).
