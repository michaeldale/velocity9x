# Mach64: an alpha test that cannot discard is not sent, and Half-Life's HUD draws

Date: 2026-10-04. Machine: A8U4I5 with the ATI 3D Rage XL PCI
(`1002:4752`), Mach64 engine path. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/hl-d3d-mwd5-hud/`.

## The refusal

Half-Life's Direct3D HUD was missing on the Rage XL: about 15,000 batches
a run refused as `M64Policy14` (alpha test). The policy took an alpha
test only on a texture with alpha (items 7 and 8 measured texel alpha).
ALPHA_TST_CNTL's vertex-alpha source was never set by any scene. The HUD
sprites are RGB565 with `NOTEQUAL 0`, so the tested alpha is the
vertex's, and the batches were refused whole.

## The rule

The same rule as on the Rage IIC (`rage2_draw.c`, 2026-10-03), in
`mach64_policy.c`. Without texel alpha the tested alpha is the vertex's,
or 255 under DECAL and COPY. If the comparison passes for every alpha
from the batch's least vertex alpha up, the test can discard nothing.
That holds for GREATER and NOTEQUAL above the reference, GREATEREQUAL at
it, and ALWAYS. Such a test is dropped (`alpha_test_dropped`) and not
programmed. Any other comparison without texel alpha stays refused, as
does one needing the greatest alpha (LESS, EQUAL). A dropped test also
no longer counts as reading the fragment's alpha for MODULATEALPHA.
Host-tested in `test_mach64_policy.c`.

## Measured

On the Rage IIC, drawing exactly this class (additive screen-space
sprites with Z off) hard-locked A8U4I5 four times
(`docs/issues/2026-10-03-a8u4i5-hard-lock-on-additive-sprites.md`).
Michael stood by to reset the Rage XL for the first run. It did not lock:

| mwd5, three runs | Before | With the rule |
|---|---|---|
| fps | 14.843, 18.125, 18.141 | 14.481, 18.025, 18.046 (recorded, not compared) |
| refused | 15,018 (reason 14) | **0** |
| timeouts, resets | 0 | 0 |

`hl-running.png` shows the HUD drawn: health, armour and the flashlight
icon.
