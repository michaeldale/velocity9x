# ATI Rage Mobility-M Phase 4 RGB565 texture evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-texture-20260928-c` first ran `ATI4TS.EXE`, which uploaded
and guarded an 8x8 RGB565 texture at aligned local-VRAM offset `0x204000`,
emitted the complete 19-write nearest/replace/clamp state, and deliberately
submitted no setup vertices or draw trigger. It passed with zero changed
pixels, zero texture/guard/restoration mismatches, no timeout, and no recovery
reset. `ATI4TS.TXT` has CRC32 `9ADDC71E`; `ATI4TS.BMP` has CRC32 `E9479885`.

The matching `ATI4TX.EXE` then submitted one triangle with normalized S/T
spanning -0.25 through 1.25 and W=1. The exact red, green and blue interior
probes passed, exercising both texture sampling and clamp. It changed 256
pixels bounded by `(8,6)` through `(38,21)`, with zero exterior,
texture/guard, state, or VRAM restoration mismatches and no timeout/reset.
Two same-boot executions were byte-identical:

- TXT CRC32 `7C0905D2`, SHA-256
  `547E0DFB3F78A239972CF04E647B9599C4AB767BE129143C9579BF744B35E8AE`;
- BMP CRC32 `A4D8EAD8`, SHA-256
  `E4185919A78F44D4165017FD36673F1F84B0722DC2286AEEF370034E400752DF`.

The two preceding machine crashes produced no diagnostic artifacts. Source
inspection proved they occurred before texture state submission: the new
texture scene left the depth page unmapped but inherited a depth-initialization
condition that accepted every scene number greater than or equal to two and
wrote through a null mapping. Commit `2a426e2` bounds depth initialization to
the actual depth scenes, modes 2 through 4. Those crashes are harness failures,
not negative texture-engine evidence.
