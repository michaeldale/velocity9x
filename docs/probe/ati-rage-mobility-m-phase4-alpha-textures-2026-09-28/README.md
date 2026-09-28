# ATI Rage Mobility-M Phase 4 alpha-texture evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-alpha-textures-20260928-b` tested ARGB1555 and ARGB4444
independently. Each 8x8 texture contained transparent red in its left half
and opaque green in its right half. `TEX_MAP_AEN` exposed texture alpha and a
single `GREATER` comparison against reference 127 served only as the alpha
observation instrument: transparent probes had to retain the `0xA55A`
sentinel while the opaque probe had to render RGB565 green `0x07E0`.

Both formats passed twice, byte-identically, on boot 11. Each changed exactly
64 pixels bounded by `(24,6)` through `(38,13)`, with zero interior, exterior,
texture/guard or restoration mismatches and no timeout/reset. The accepted
transcripts differ at the expected texture datatype only:

- ARGB1555: `DP_PIX_WIDTH=0x30040444`; TXT CRC32 `1E5FEE39`, SHA-256
  `0E585A948C74A04EB84CF93689259C65B6152ECA7F0FB218368FAF784E64A096`;
- ARGB4444: `DP_PIX_WIDTH=0xF0040444`; TXT CRC32 `FB2A12D8`, SHA-256
  `19EFC100B34B0CCF0A2DE132192E26B0E810FDD83A468B16572B3F0D9A9F75D0`;
- their identical BMPs have CRC32 `7781336E`, SHA-256
  `252223DAD5BCAAAF4BD257B2E07FF48B07AA47CD0BC3A8DFB26A75BEED237338`.

The first ARGB1555 build A run is retained as REVIEW evidence. Its intended
alpha-control word was accidentally placed in the adjacent `Z_CNTL` state
slot, while `ALPHA_TST_CNTL` remained zero; it safely drew no pixels and
restored all state and memory. Commit `4318efe` corrected only that diagnostic
array index. This is a harness failure, not negative texture-format evidence.

This closes Phase 4 item 7. Phase 4 item 8 remains responsible for validating
the complete alpha-test comparison table rather than the single observation
comparison used here.
