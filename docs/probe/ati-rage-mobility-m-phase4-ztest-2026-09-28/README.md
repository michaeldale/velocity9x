# ATI Rage Mobility-M Phase 4 Z16 test without writes

Target: Gateway Solo 2150, ATI Mobility-M `1002:4C4D` revision `64`, subsystem
`107B:2150`, 4 MiB SGRAM, 1024x768x16 desktop, agent boot counter 9.

Build `ati-phase4-ztest-20260928-a` reused the proven guarded RGB565 target at
VRAM offset `0x00200100` and added a separately backed-up 4 KiB depth page.
The Z16 target begins at `0x00202100`, has a 128-byte pitch, and is surrounded
by 256-byte guards. Every stored Z value was initialized to `0x8000`.

The draw used `Z_CNTL=0x00000011`: Z testing enabled, LESS comparison, and
`Z_MASK_EN` clear. All three incoming vertex depths were `0x4000`, encoded in
the setup packet as `0x20000000`. The passing color triangle therefore proves
the comparison accepted incoming `0x4000` against stored `0x8000`; the exact
unchanged depth target proves the disabled write mask was honored.

Two accepted same-boot runs passed and produced byte-identical artifacts:

- `ATI4Z0.TXT` and `ATI4Z0-PASS2.TXT`: CRC32 `00CA55E6`, SHA-256
  `47664C2227FC599F046DC97198DCE5EB6969B15CF7ACD040D39366767AB770A7`;
- `ATI4Z0.BMP` and `ATI4Z0-PASS2.BMP`: CRC32 `BD518408`, SHA-256
  `3BDC87A735A150E512684F6DCC314C5BE6BF6615B993310DB0DE774B138BA727`;
- 256 changed color pixels, bounding box `(8,6)` through `(38,21)`;
- zero interior, exterior, color-guard, depth-target, depth-guard,
  persistent-state, and VRAM-restore mismatches;
- zero timeouts and zero recovery resets;
- `GUI_STAT` idle before and after (`0x00800000`), with the desktop and remote
  agent still responsive after both runs.

Host truth-table tests cover NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL,
GEQUAL, and ALWAYS for incoming depths below, equal to, and above stored Z.
This physical experiment deliberately exercised only LESS and made no depth
writes. Z16 writes and clear remain the next Phase 4 depth gate.

Public ATI acceleration remains disabled.
