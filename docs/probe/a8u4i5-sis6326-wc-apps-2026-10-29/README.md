# A8U4I5: SiS 6326 applications under write-combining, and the U prestep, 2026-10-29

- Machine: A8U4I5, P3 1 GHz, 440BX, SiS 6326 card 2 (rev 0Bh), desktop
  800x600x16.
- Boot 650: `0524e7d1`, frame buffer write-combined
  (`MtrrWrite=ok s=3 b=de000001 m=ffc00800`).
- Boots 651-653: the same, with the HAL from the working tree:
  - boot 651: a constant 1/16-texel U bias;
  - boot 652: half a pixel of dU/dx;
  - boot 653: the per-triangle prestep that was kept.
- Records:
  - [write-combining](../../decisions/2026-10-29-sis6326-write-combining.md);
  - [U prestep](../../decisions/2026-10-29-sis6326-u-prestep.md).

## Applications, boot 650, against 2026-10-05

| Application | 2026-10-05 | Boot 650 |
|---|---|---|
| V9XDDP | COMPLETE | COMPLETE. No check that passed before fails. `D3DZCompareOk` and `D3DZWriteMaskOk` now pass; they did not run before (`88760231h`) |
| V9XGLP | PASS | PASS, `Renderer=Velocity9x SiS 6326` |
| Final Reality 1.01, full benchmark | 3D 1.85, overall 3.55 (boot 289) | 3D 1.90, overall 3.61; 2D 6.62, bus 3.83 |
| 3DMark 99 Max, every test, 800x600x16 | 274 3DMarks, 13,344 CPU (boot 291) | **200** 3DMarks, 13,057 CPU |
| Half-Life D3D, mwd5, 640x480 | 7.905 / 8.635 / 8.632 fps (boot 294) | 7.841 fps (first run of the boot) |
| Half-Life GL, mwd5, 640x480 | 6.008 / 6.468 / 6.442 fps (boot 295) | 6.639 fps |
| Quake 2 demo1 timerefresh | 7.78-7.82 fps (boot 296) | not run: `couldn't exec v9xtr.cfg` |

`B650-V9XSNAP-AFTER-3DMARK99.INI` covers the boot up to the end of 3DMark 99
(DBCAPS, DB3D, V9XDDP, V9XGLP, Final Reality, 3DMark): `EngineIdleTimeouts=0`,
`FlipDeclined=0`, `BatchesEngineRefused=8`.

The 3DMark 99 drop is recorded and not explained. Between the two runs:

- the colour blend factors were taken (2026-10-05, boot 293): 2x-modulate
  batches that were dropped are now drawn;
- 24 days of shared Direct3D core changes landed;
- write-combining was turned on.

No write-combining-off run of 3DMark 99 was made.

## Console text

| File | HAL | Text |
|---|---|---|
| `B650-HL-D3D-CONSOLE-MICHAEL.png` | `0524e7d1` | Michael's capture: specks beside glyphs |
| `B650-HL-GL-CONSOLE.png` | `0524e7d1` | each glyph one texel left: "/" shows as "'", the L of HALF-LIFE loses its upright |
| `B651-HL-D3D-CONSOLE.png` | + 1/16-texel bias | left shift gone; new specks on the diagonal half of glyphs |
| `B652-HL-D3D-CONSOLE.png` | + half a pixel of dU/dx | most words clean; "mwd5.dem", "TrackerUI.dll", "not loading" lose a column |
| `B653-HL-D3D-CONSOLE.png`, `B653-HL-GL-CONSOLE.png` | per-triangle prestep | every glyph whole; the two renderers agree at 8x |

Boot 653: Half-Life D3D 7.870 fps, GL 6.527 fps.

- `B652-HL-LAUNCH-FAULT.png`: Half-Life faulted once at its launcher splash
  (`EIP=00000003`), before any drawing. It came right after a launch that the
  run script closed mid-load. The next launch on the same boot ran. Not
  investigated.
- The console background is still not right. Under Direct3D it is
  horizontal streaks; under OpenGL it is blocky purple and green noise.
  Neither matches the menu's backdrop. Parked (Michael, 2026-10-29: "good
  enough to park for now").
