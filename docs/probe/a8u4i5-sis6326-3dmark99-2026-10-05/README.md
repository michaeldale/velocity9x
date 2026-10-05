# A8U4I5: 3DMark 99 Max on the SiS 6326 Direct3D engine, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), Velocity9x build `0eb3030`
  (TEND after every triangle, mip magnification filters folded), boot
  291, after Final Reality and V9XDDP on the same boot.
- Plan: [sis-6326-hardware-3d.md](../../plans/sis-6326-hardware-3d.md),
  phase 5.

| File | What it is |
|---|---|
| `B291-3DMARK99-SETTINGS.png` | The project as run: every test, 800x600x16, 16-bit Z, triple buffer, device "Velocity9x SiS 6326" |
| `B291-3DMARK99-SHOTS.png` | Agent screenshots every 25 s. Most frames are the loading screens: screenshots during page flips read the page GDI sees. Frame 5 shows the emboss bump-mapping test "not supported by hardware - skipping test" |
| `B291-3DMARK99-SCORE.png` | The score window |
| `B291-V9XSNAP-AFTER-3DMARK99.INI` | V9XTRACE after the run, cumulative for the boot |

## Result

The full suite ran to its score with no idle timeout and no declined flip.
`BatchesEngineRefused` stayed at 5 and `M64PolicyLast` at 9: those are
V9XDDP's refusals earlier in the boot, so 3DMark had none.

Scores as reported, recorded and not compared: 274 3DMarks, 13,344 CPU
3DMarks.

Not checked: the image quality test's output, and what each game test drew.
The screenshots cannot see a flipping application's frames.
