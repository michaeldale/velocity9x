# ATI Rage Mobility-M Phase 4 blend evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-blend-one-one-20260928-b` is the first Phase 4 item 9
factor-pair gate. It enabled ADD blending with source ONE and destination ONE
through `SCALE_3D_CNTL=0x000908C1`. An opaque red triangle was drawn over the
guarded target's existing RGB565 `0xA55A` value. The correct saturated sum is
`0xFD5A`; this differs from the source, the destination, and an unblended
replacement.

The build passed twice, byte-identically, on boot 11. Three separated
interior probes returned `0xFD5A`, exactly 256 pixels changed within `(8,6)`
through `(38,21)`, all exterior and guard samples and restored state/VRAM
matched, and no timeout or recovery reset occurred. The accepted TXT has
CRC32 `0E104B04` and SHA-256
`E88C1C9E942C7EBE63EC60F487EC1145E632D1D39BA59285AE2054EF2B53EB8F`;
the BMP has CRC32 `ABA09C71` and SHA-256
`9935359797F33C99C4AEEAED03591000307D824111D1434DDA082860A485D9C3`.

The build A report is retained as safe REVIEW evidence. Its render and BMP
were already correct and all restoration checks passed, but a diagnostic
range bug sent scene 20 through the depth-page verifier even though no depth
page was mapped. That produced 2,041 false guard mismatches from the null
mapping. Commit `32b5eac` bounded depth verification to scene modes 2 through
4; build B changed no blend state, geometry, target data, or pixel
expectation, and its BMP is byte-identical to build A.

Build `ati-phase4-blend-alpha-20260928-a` independently tested the common
SRCALPHA/INVSRCALPHA pair. A red source with alpha 128 was blended over the
same `0xA55A` destination using `SCALE_3D_CNTL=0x002C08C1`. All three probes
returned the CPU-predicted RGB565 mix `0xD2AD`, which retains source red and
destination green/blue. It passed twice byte-identically on boot 11 with the
same 256-pixel bounds and all mismatch, timeout and reset counters zero. The
TXT has CRC32 `04B6E341` and SHA-256
`0278A247F3C66694F94170D6FCBAD64EF11D2821B79856D136788415A0AB6577`;
the BMP has CRC32 `F954B178` and SHA-256
`B569301DA34DBE722E127BB4E1DF81B5F2A630F7A46551C7E267BCB6BA40309B`.

This proves ONE/ONE and SRCALPHA/INVSRCALPHA, but does not close item 9. Each
other factor pair selected for publication still requires an isolated
physical scene. Public ATI acceleration remains disabled.

## Factor table

The advertised set is six source factors (ZERO, ONE, SRCALPHA, INVSRCALPHA,
DESTCOLOR, INVDESTCOLOR) crossed with six destination factors (ZERO, ONE,
SRCCOLOR, INVSRCCOLOR, SRCALPHA, INVSRCALPHA). Direct3D publishes source
and destination caps independently, so every one of the 36 pairs is tested.
BOTHSRCALPHA and BOTHINVSRCALPHA are aliases of two of them. DESTALPHA,
INVDESTALPHA and SRCALPHASAT are excluded: RGB565 and XRGB1555 targets store
no alpha, and what the hardware reads in its place is unmeasured.

Build `ati-phase4-blend-table-20260928-a` (`ATI4BT.EXE`, VxD DIOC 26) drew
each pair as its own guarded scene: backup, `0xA55A` seed, one flat triangle
with source ARGB `0xD4B16100`, drain, invalidate, inspect, restore. The VxD
accepts only a `SCALE_3D_CNTL` whose non-factor bits equal the proven ADD
word `0x000008C1` and whose factors are not destination alpha; anything else
fails the DIOC before MMIO is mapped. The VxD judged three-probe uniformity;
the publisher compared the observed pixel with ten candidate CPU rounding
rules, each of which reproduces both earlier results. `0xD4B16100` was
chosen so that, under every candidate rule, changing either factor field of
any pair to another listed factor changes the pixel.

Both boot-11 runs were byte-identical and returned PASS. All 36 pairs had
status `0x0001FFFF`, uniform probes, zero exterior, guard and restoration
mismatches, no reset and no timeout. The 34 pairs whose result differs from
the destination changed exactly 256 pixels within `(8,6)` through `(38,21)`;
DESTCOLOR/INVSRCCOLOR and ZERO/ONE reproduce `0xA55A` and changed none.

Three rules predicted all 36 pixels: factor/255 with 255-minus inverse,
truncated at blend and at 565 packing (rule 0); factor/256 with 255-minus
inverse, rounded at blend; and factor/256 with 256-minus inverse,
truncated. The other seven each missed one to three pairs. The table
therefore fixes factor selection for every pair but does not yet single out
the rounding rule; a software fallback that must match hardware blends
needs a scene that separates those three.

| File | CRC32 | SHA-256 |
|---|---|---|
| `ATI4BT.TXT`, `ATI4BT-PASS2.TXT` | `A0FB4E04` | `4A389C251B17327E566817284DD288B23527B3E858CE64B296C4E3BE9B07CFCA` |
| `ATI4BT.BIN`, `ATI4BT-PASS2.BIN` | `9076CD43` | `958E29BEE308E813EADD047AD0960B148D8FE9E7E555FBB43FE6D4D407D8E25D` |

`ATI4BT.BIN` holds the 64x28 RGB565 target for each pair in table order,
3,584 bytes apiece, top row first. Phase 4 item 9 is closed; public ATI
acceleration remains disabled.
