# RIVA 128ZX: opaque A4R4G4B4 under the alpha test, from a tested copy

Date: 2026-11-07. Machine: M-7V636C (remote help), RIVA 128ZX
(12D2:0018, 8 MiB), Windows 98 first edition, desktop 800x600x16 at
5:5:5, boot 44. Plan: `docs/plans/nvidia-riva128-d3d.md`, step 5.
Evidence: `docs/probe/m7v636c-riva128-4444-alpha-test-2026-11-07/`.

## The gap

The chip has no blend that ignores alpha. An opaque draw is the source
alpha blend with the vertex alpha forced to FFh, so the texel alpha
must be 1 (`nv3_3d.c`, `v9x_nv3_3d_build_state`). A4R4G4B4 has partial
alphas, so the HAL draws opaque A4R4G4B4 from a second copy converted
to X1R5G5B5 (boot 25). Under the alpha test that copy has no alpha to
test. The builder refused the state (`M64Policy11`): 792 batches in
Half-Life's `c1a0` on boot 26, and 2,204 on boot 27
(`docs/decisions/2026-11-04-riva128-half-life-d3d.md`).

## Change

Under the alpha test the copy is built as A1R5G5B5. Its top bit is
Direct3D's verdict on that texel. The 4-bit alpha is widened to 8 bits
(a x 11h) and compared with the draw's function and reference. The new
pure function is `v9x_nv3_3d_tested_texel`, host-tested for every
comparison.

The draw samples the copy as A1R5G5B5. The chip then tests GREATER 7Fh:

- With the vertex alpha forced to FFh, a texel reads alpha 0 or FFh.
  What passes is drawn whole, and the blend never sees a partial alpha.
- The midpoint reference keeps the split where Direct3D's would be
  under bilinear, whether or not the chip filters alpha.

The texture record holds which test its copy carries. A copy is rebuilt
when the texture is written, or when a draw wants another test. A plain
opaque draw reads any copy as X1R5G5B5, which ignores the top bit, unless
the draw is colour keyed: the chip's key compares the top bit too
(boot 31). A key on the tested copy gets the top bit set. Texels with the
bit clear fail the test anyway, so this is exact.

Two things are not exact:

- **The vertex alpha.** It takes no part in the test, as for A1R5G5B5
  under the test already. Direct3D's MODULATE would test texel alpha x
  vertex alpha.
- **The edges under bilinear.** They are softened. A filtered alpha
  between 80h and FEh passes and is blended, where Direct3D would replace
  the pixel.

## Boot 44: HAL SHA-256 `9DD04601...`, installed by WININIT rename, hash checked

- **V9XDDP:** 594 batches, 5 refused, all `REFUSE_TEXTURE` (policy 3),
  the same as boot 34 (`b34-SNAP.INI`). The 98 `*Ok` keys match boot 34
  exactly, 82 passing. V9XDDP has no alpha-test check, so this shows only
  that nothing regressed.
- **Half-Life:** `hl.exe -window -w 640 -h 480 -console -condebug +map
  c1a0`, about four minutes. Counters (`snap-hl.ini` minus `snap-ddp.ini`):

  | Counter | Value |
  |---|---|
  | Batches drawn | 495,855 |
  | Triangles | 6,890,100 |
  | Refused | 0 (policy 11 = 0, against 2,204 on boot 27) |
  | Alpha-test sets | 65,552 |
  | Alpha-test functions seen | GREATER, NOTEQUAL, ALWAYS |
  | Last reference | 0 |
  | Engine timeouts | 0 idle, 0 FIFO |

  No engine timeouts.
- **Screenshots:** the view from the lobby door is the same as boot 27's
  (`hl-2.png` against `../m7v636c-riva128-games-2026-11-04/hl-door-before-after.png`).
  4.7-5.6 fps windowed.

## Not established

- **Which surfaces these draws are, and whether they look right.** The
  counters show the draws are accepted, not what they cover. The
  screenshot from the lobby door shows no difference.
- **How often a copy is rebuilt.** The rebuilds are not counted. A
  texture drawn under two different tests, or keyed and tested in turn,
  rebuilds at each change.
