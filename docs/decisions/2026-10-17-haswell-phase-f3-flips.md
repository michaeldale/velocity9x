# Haswell Phase F3: flips through plane A's double-buffered base; the counter reads a line behind

2026-10-17 to 18, i5-4570 (`8086:0412`), boots 16 and 17, at 1024x768x16
(the probe) and 800x600x16 (3DMark 99). As designed in
`docs\plans\intel-haswell-direct3d.md` Phase F3, for
`docs\issues\2026-10-17-haswell-3dmark99-flicker-and-game2.md`. Evidence:
`docs\probe\hsw-4570-flip-f3-2026-10-17\`.

## Step 1: V9XHSWMM /flip:2 (boot 16)

DIOC 4 read the plane, wrote `DSPAOFFSET` then `DSPASURF` for row 383,
held 5 s, and wrote back what it had read.

| | read | flip to row 383 | restore |
|---|---|---|---|
| `PIPEACONF` / `DSPACNTR` | `C0000010` / `94000000` (5:6:5, enabled) | | |
| `DSPASTRIDE` | `840h` (2112) | | |
| `TRANS_VTOTAL_A` | `032502FF`: vactive 768, vtotal 806 | | |
| written | | surf `C5000h`, x `3E0h` (992) | surf 0, x 0 |
| `PIPEADSL` at the write | | 539 | 45 |
| polls until `DSPASURFLIVE` matched | | 21,802 | 68,689 |
| line before / at the latch | | 766 / 767 | 766 / 767 |
| frames begun while polling | | 0 | 0 |
| readback after | | surf, offset and live all as written | all 0 |

- **The base is double-buffered and latched once a frame.** Written at
  line 539 or line 45, the live base changed in the same frame, between
  two consecutive polls, and never mid-frame. So a flip needs no write
  window: it is pending until `DSPASURFLIVE` reads the base written.
- **`PIPEADSL` read 767 at the latch, of a 768-line mode.** Vol 2c puts
  the latch at the start of the vertical blank, and i915 adds a
  `scanline_offset` of 1 to this counter on DDI parts. Either the counter
  is a line behind or the latch is a line early. This run cannot tell
  them apart, and both give the same blank test, which now adds one line.
- **The picture was not seen.** The OBS capture was not connected, and a
  GDI screenshot reads the page at offset 0. So "the desktop moved up by
  exactly 383 rows with no horizontal shift" (the design's third
  criterion) is not confirmed. 3DMark's frames below would show a wrong x
  as a shifted picture, but that also needs eyes.

## Step 2: the HAL

- **`src\chipsets\intel\hsw_display.c`** (host-tested,
  `test_intel_hsw_display.c`): the offset split, liveness from
  `PIPEACONF` and `DSPACNTR`, and the blank test with the one-line offset.
- **`src\display32\engines\hsw_scanout.c`:** register mechanics only.
  It writes `DSPAOFFSET` then `DSPASURF` and remembers the base. Pending
  is `DSPASURFLIVE` != that base. `PIPEADSL` is read only when the pipe
  and plane are live.
- **Dispatch:** each entry point in `i9xx_scanout.c` tries Haswell
  first.
  - It is a hardware flip (`V9X_FLIP_WAIT_HW`).
  - It never needs a blank window.
  - The vblank source is available only while pipe A and plane A are
    live.
  - An unsynced flip is the same write.
- **The cap:** `hsw_hw16.c` and the family manifest add `FLIP`, giving
  `EngineCaps=00000017`.

## Boot 17: 3DMark 99, the same default run

800x600x16, 16-bit Z, triple buffer, every test; DRV and HAL both
`59f2ffcf-dirty`.

| | boot 15 (no flips) | boot 17 |
|---|---|---|
| `FlipHandled` / `FlipDeclined` | 0 / 77,714 | 6,799 / 0 |
| `FlipTakenAtDone` / `FlipNotTakenAtDone` | - | 6,798 / 0 |
| `FlipForcedIdle` (abandoned flips) | 0 | 0 |
| `DrawsIntoPresented` | 300,727 | 0 |
| `FlipStillDrawing` | 0 | 51,508,074 |
| refresh rate 3DMark reports | "VSync Off" | 59 Hz |
| 3DMarks (recorded, not compared) | 3059 | 3015 |
| Game 1 / Game 2 | 56.9 / 20.9 fps | 54.9 / 20.8 fps |

- **No batch was drawn into the presented buffer**, against a third of
  them before.
- Every flip completed against the live base, none was abandoned, and
  none was declined.
- `FlipStillDrawing` is DirectDraw calling Flip again while the previous
  flip waits for its blank. Presentation is now paced by the display,
  and 3DMark reads 59 Hz where it read "VSync Off".
- The 2-8 MB texture tests still report 230-580 fps with only 6,799 flips
  in the whole run. So they do not present every frame they count. How
  3DMark counts those frames is not established.

## Not done

- **Whether the flicker is gone is for Michael's eyes**: the counters
  say no frame is drawn on screen, and nobody has watched.
- Game 2's slowness is the submission path (see the issue) and is not
  touched here.
- Modes other than these two were not measured.

Gates: check-tree, build-host (the one-line DSL test failed before the
offset was added), run-checks.
