# A8U4I5: SiS 6326 2D engine write probe, 2026-10-05

- Machine: A8U4I5, boot 208, SiS 6326 card 2 (rev 0Bh), Velocity9x `sis`
  tier-0 (no SiS register written by the display path).
- Tool: `SIS2D.EXE` + `SIS2D.VXD`, `scripts/build-sis6326-2d.ps1`, build
  `sis2d-20261005-a`. Engine commands from `src/chipsets/sis/sis6326_engine.c`,
  compiled in.

| File | Desktop | Result |
|---|---|---|
| `SIS2D-8BPP.TXT` | 640x480x8 | PASS: fill, forward copy, right-overlap copy, down-overlap copy |
| `SIS2D-16BPP.TXT` | 640x480x16 (set with `V9XMSW /set:640x480x16` just before) | PASS: the same four |

Each test region is 12 rows x 64 bytes at pitch 256 in VRAM at 2 MiB and
above, guarded with A5h (copy sources with a non-repeating gradient), and
compared byte for byte with the model of what the builder asked for. The
`Row*` lines are the regions as read back.

The desktop was left at 640x480x16.
