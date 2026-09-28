# ATI Rage Mobility-M Phase 4 perspective evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-perspective-20260928-a` submitted the Phase 4 RGB565
texture scene with W values `(1.0, 0.25, 0.25)`. The complete state and
setup transcript was accepted safely, with 256 changed pixels bounded by
`(8,6)` through `(38,21)`, intact exterior, texture and guard memory, exact
restoration, and no timeout or recovery reset. It reported REVIEW only
because two assertions reused affine probes next to the perspective-shifted
quadrant boundaries. `ATI4PW-REVIEW-A.TXT` has CRC32 `BB89FB13` and SHA-256
`ABAA1999760786168AD07325D8FA10AF6DEECA18FBCB5893AF60AEE61F59988A`.

Comparison with the W=1 texture capture showed the unequal-W result had
moved robust samples at `(30,8)` from green to red and `(10,16)` from blue
to red, while `(10,8)` remained red. Build
`ati-phase4-perspective-20260928-b` changed only those two assertions; the
register state, vertices, and resulting BMP remained byte-identical.

Two same-boot executions of build B passed byte-identically with
`Status=0x0001FFFF`, zero interior, exterior, texture/guard and restoration
mismatches, 19 state writes, 19 setup writes, and no timeout/reset:

- TXT CRC32 `4A42453B`, SHA-256
  `F138A433A8F2FDBD137A1F191EE4F2A06FD6E7E34DAA6CF0D7685F5F5934AD63`;
- BMP CRC32 `4AA1ED66`, SHA-256
  `216E32EA15ACD8C71E07FDC4957E592AC73F2687A04E47EECA21C4B8684E3C71`.

The accepted BMP is also byte-identical to the REVIEW BMP. This isolates the
result change to choosing stable interior assertions and preserves the first
capture as evidence of the measured perspective displacement.
