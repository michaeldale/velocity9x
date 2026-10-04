# A8U4I5: SiS 6326 3D engine, phase 3 (textures), 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), Velocity9x `sis`, desktop
  800x600x16. Boot 210 for runs A and B, then rebooted to clear the hung
  engine; boot 211 for runs C to I.
- Tool: `SIS3D.EXE` on `SIS2D.VXD` (build `sis2d-20261005-b`). State from
  `v9x_sis3d_build_state` and `v9x_sis3d_build_texture`.
- Targets: 32x32 RGB565 at pitch 64, VRAM 2 MiB + 4 KiB x region. Textures
  from 2 MiB + 256 KiB:
  - the index texture: 16384 words, each holding its own offset;
  - the hashed 16-bit and ARGB8888 textures;
  - alternating columns and rows;
  - a solid ARGB4444 texel;
  - two six-level mip chains.

| File | Build | Switch | What it ran |
|---|---|---|---|
| `SIS3D-A-HANG.TXT` | `p3a` | `/phase3` | Six addressing scenes; level field 0 and 8A38h D4 held set, written once. **No scene went idle**; the dumps are torn. |
| `SIS3D-B-STILL-HUNG.TXT` | `p3b` | `/phase3` | Refused at enable: 89FCh 00200074h, never idle. |
| `SIS3D-C-ADDRESSING.TXT` | `p3c` | `/phase3` | After the reboot: level field 1, D4 pulsed. Norm64/32/280, Texel64, Mag4, Prestep (raw field 512), Levels0, ClearHeld. |
| `SIS3D-D-PITCH-SWEEP.TXT` | `p3d` | `/phase3` | 24 raw pitch fields (columns 0 and 31), then C's scenes again. |
| `SIS3D-E-MAPPING.TXT` | `p3e` | `/phase3` | Builder-encoded pitches: Addr, Mag4, Prestep, Wrap/Mirror/Clamp with U on texel edges (u x 32 = 2x - 15), NoMapping. Stopped there by a probe bug (below). |
| `SIS3D-F-FORMATS-FILTERS.TXT` | `p3f` | `/phase3` | E's scenes with U off texel edges, Persp/PerspFlat/PerspOff, Format565/1555/4444, LinearU/V, Blend0-15 at Apix 80h with mask bit 0. |
| `SIS3D-G-BLEND-MIPS.TXT` | `p3g` | `/phase3` | F's scenes, Blend0-63 at Apix 40h with mask bit 7, Format555/8888, marker-chain mips. |
| `SIS3D-H-MIP-DETAIL.TXT` | `p3h` | `/phase3m` | Mips only: G's mip scenes, dumps at 1.5 and 2, fine ratios, minification codes 3-5. |
| `SIS3D-I-MIP-BLEND.TXT` | `p3i` | `/phase3m` | H's scenes, then the black/white contrast chain under codes 2-5. |

How the committed source differs from the builds that ran:
- Builds `p3c`-`p3e` tested the idle wait after the readback, which reuses
  the result buffer. They compared a pixel pair against FFFFFFFFh, so E's
  `NoMapping` reported `NOT-IDLE` while `SecondWaitReads` read 1.
- `p3c`-`p3i` wrote level field 1 with no level-1 base. The committed
  builder treats the field as the last level's index, so the source now
  also writes a level-1 base and pitch, which no non-mip scene samples.

Reading the files:
- `*Pixels<row>` are the read-back pixels. On index-texture scenes each one
  is the word the engine fetched (`tex_decode.py`).
- `*Mismatched` is the probe's own advisory check. It does not model the
  missing x prestep (Mag4, Prestep) and uses coarse integer perspective
  (Persp), so those counts are expected. The fits are the evidence:

      python tex_prestep_fit.py SIS3D-E-MAPPING.TXT
      python tex_persp_fit.py SIS3D-F-FORMATS-FILTERS.TXT
      python tex_decode.py SIS3D-E-MAPPING.TXT Mag4 Wrap Clamp Mirror NoMapping
