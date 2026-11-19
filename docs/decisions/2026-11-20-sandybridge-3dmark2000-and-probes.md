# The HD 3000 on its Gen6 Direct3D: 3DMark 2000 4015, every release probe passes

Date: 2026-11-20
Status: **measured once, M-NZHWBR boot 64.** Step 7 of
`docs\plans\intel-sandybridge-direct3d.md`. Build `dc48a9b4-dirty`
(branch `intel-snb`; the probes rebuilt with that id). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-games-2026-11-20\` (the run's own record
is `release-checks-baseline-record.md`; the ten BMP grabs are left out,
`3dmark2000-grab\contact-sheet.png` shows them).

## How it was run

`scripts\run-release-checks.ps1 -Baseline -ThreeDMark -ThreeDMarkMinutes 0`
against a one-machine list (`hd3000`, PciId `8086:0112`, D32Runs default,
`/bpp16`, `/ddi6`, `/ddi5`): no install, the driver boot 64 already had.
The list's controller was the agent scripts copy with the relay's
PipelineWindow at 4. The card check found `8086:0112`. 21.5 minutes, nine
of nine probe runs exit 0.

## Results

| Check | Result |
|---|---|
| V9XD32 default, `/bpp16`, `/ddi6`, `/ddi5` | PASS, PASS, PASS, PASS |
| V9XGLP (OpenGL ICD) | PASS |
| V9XGLIDP (Glide) | PASS, 0 failures |
| V9XR3DP (render interface) | COMPLETE |
| V9XWPRS (windowed presents) | PASS, 0.27 ms DirectDraw / 0.24 ms OpenGL |
| V9XFBCMP | PASS |

**3DMark 2000: 4015** (`3dmark-score.png`), the default benchmark at
1024x768, 16-bit colour, 16-bit Z, triple buffered, run to its score in
5.2 minutes. Recorded, not compared. Counters over the run: 942,040
draws, none into the presented buffer, no refused batches, no engine
stalls, FilterMagSeen `6`. The ten Game 1 grabs show textured terrain,
trees, sky, water, the helicopter and the HUD's translucent panels drawn
as expected; nothing missing or misplaced was seen at contact-sheet size.

3DMark names the display "Velocity9x VBE-generic display": the driver
files were swapped over the vbe install, so the device's registry
description is still the vbe INF's.

## Not measured

- Z24, OpenGL games (Quake 2 and 3) and Glide games.
- The picture on the monitor; the grabs are the flipped buffers.
- The i915 render-ring workaround registers, still unwritten.
