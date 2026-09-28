# ATI Rage Mobility-M Phase 4 Z16 writes

Target: Gateway Solo 2150, ATI Mobility-M `1002:4C4D` revision `64`, subsystem
`107B:2150`, 4 MiB SGRAM, 1024x768x16 desktop, agent boot counter 9.

This gate enabled `Z_MASK_EN` on the passing LESS scene. The diagnostic seeded
the separately guarded Z16 target to `0x8000`, correlated all 1,792 depth words
with the color image, and required each of the 256 rasterized pixels to become
`0x4000` while every uncovered pixel stayed `0x8000`. It then restored and
verified both 4 KiB VRAM pages and all persistent engine state.

Build `ati-phase4-zwrite-20260928-a` is retained as `ATI4ZW-REVIEW-A.*`. Its
color result was correct and its memory/state restoration passed, but all 256
covered Z words differed from expected. Build `b` added first-mismatch capture
without changing the draw and reported actual Z `0x2000` versus expected
`0x4000`. This proved that the historical Mesa-style `depth << 15` setup value
does write depth, but stores exactly half of the requested Z16 value on this
Mobility-M.

The shared builder was corrected to encode Z16 in setup bits `[31:16]`.
Build `ati-phase4-zwrite-20260928-c` then passed twice with byte-identical
accepted artifacts:

- `ATI4ZW.TXT` and `ATI4ZW-PASS2.TXT`: CRC32 `7A32B39B`, SHA-256
  `E78E39A1F31A6E2D566BF1A87B59D7ED6B6856C3D4D8555E243B72AA0C088F82`;
- `ATI4ZW.BMP` and `ATI4ZW-PASS2.BMP`: CRC32 `BD518408`, SHA-256
  `3BDC87A735A150E512684F6DCC314C5BE6BF6615B993310DB0DE774B138BA727`;
- 256 changed color pixels, bounding box `(8,6)` through `(38,21)`;
- zero interior, exterior, depth, guard, persistent-state, and VRAM-restore
  mismatches;
- zero timeouts and zero recovery resets;
- `GUI_STAT` idle before and after (`0x00800000`), with the desktop and remote
  agent responsive after every run.

The two REVIEW reports are negative harness evidence. Their BMPs match the
accepted color image because the defect was confined to Z encoding.

The Z16 write half of Phase 4 item 3 is complete. A combined draw/2D-depth-
clear ordering diagnostic remains open before the item is closed. Public ATI
acceleration remains disabled.
