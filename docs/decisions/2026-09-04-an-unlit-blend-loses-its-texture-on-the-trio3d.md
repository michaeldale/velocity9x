# A blended UNLIT draw loses its texture colour on the Trio3D/2X

Date: 2026-09-04
Status: measured on A8U4I5 against the emulated ViRGE/DX; the defect is one
axis wide and the driver has an obvious answer that is not made here

## The rung

3DMark 99 draws its sprites and HUD in opaque black boxes on this card at both
resolutions, on a boot where every alpha rung the probe has reads correct
(`2026-09-04-four-megabytes-is-the-resolution-limit.md`). "The part cannot
blend" is therefore not the explanation, and the difference had to be something
the probe was not crossing.

The command-word census said what to cross. 3DMark's blended ARGB4444 draws are
`Z:LESS` with no depth write where every alpha cell in the matrix is depth-off,
`LINEAR` where they are `NEAREST`, and half of them are `UNLIT` where they are
`LIT` with `MODULATE`. `Spr_*` crosses those three - eight cells.

It also fixes something the matrix could not. The matrix's alpha cells draw
over a **black** target and expect the transparent half to read black, which a
driver that correctly keeps the destination and one that writes an opaque black
box both satisfy. This rung fills the target **red**, so the two are different
readings.

## Measured

Left half of the texture is opaque green, right half is alpha-zero blue, drawn
over red. Correct is green then red.

```
                     emulated ViRGE/DX        Trio3D/2X
Spr_zoff_near_mod    green / red   Ok=1       green / red   Ok=1
Spr_zoff_lin_mod     green / red   Ok=1       green / red   Ok=1
Spr_zon_near_mod     green / red   Ok=1       green / red   Ok=1
Spr_zon_lin_mod      green / red   Ok=1       green / red   Ok=1
Spr_zoff_near_copy   green / red   Ok=1       BLACK / red   Ok=0
Spr_zoff_lin_copy    green / red   Ok=1       BLACK / red   Ok=0
Spr_zon_near_copy    green / red   Ok=1       BLACK / red   Ok=0
Spr_zon_lin_copy     green / red   Ok=1       BLACK / red   Ok=0
```

**Depth makes no difference. The filter makes no difference. The shade mode
makes all of it.** Every `MODULATE` cell passes and every `COPY` cell fails, and
they fail in one specific way: the *opaque* half draws black while the
alpha-zero half correctly keeps the destination. The texel's alpha is honoured
in both; it is the texel's colour that is lost.

The engine maps any `TEXTUREMAPBLEND` that is not `MODULATE` to the command
word's `TEXTURE_UNLIT` bit, so what this measures is: **on the Trio3D/2X, a
draw with `TEXTURE_UNLIT` and the alpha field enabled samples its texel's alpha
and drops its colour.** The same command word is correct on the emulated
ViRGE/DX, and `TEXTURE_UNLIT` without the alpha field is correct on the card -
90 of the matrix's unblended cells pass, most of them unlit.

## What it accounts for, and what it does not

The census counts 10,752 `UNLIT ... ALPHA_ENABLE 4444` draws in a 800x600
3DMark run and 25,438 at 640x480, against 11,525 and 14,914 `LIT MODULATE`
ones. So a large part of what that benchmark blends goes through the broken
path, which is consistent with black rectangles in the picture.

It is not proof that these are *the* black boxes. In the frames the sprite art
is visible with a black border, where this rung's failure is the opposite way
round - the opaque part black. Either the visible sprites come from the
`LIT MODULATE` population and the `UNLIT` ones are wholly black quads, or there
is a second mechanism. The rung says what it says and no more.

## The answer the driver has, and why it is not taken here

`LIT` with `MODULATE` and a white vertex colour is texel x white, which is the
texel - arithmetically the same picture `UNLIT` should draw, through the path
this card gets right. Substituting it for a blended `UNLIT` draw is a small
change in `v9x_d3d_virge_alpha_bits`' caller.

It is not free. `DECAL` and `COPY` ignore the vertex colour by definition, so
the substitution is only equivalent if the engine also forces the vertex colour
white and its gradients to zero for those draws - otherwise an application that
set a colour it expected to be ignored would suddenly see it applied. That is
three more registers and a behaviour change on a path other chips share.

Today has already produced one confident chip fact that a controlled A/B
retracted (`2026-09-04-the-trilinear-two-pass-and-a-retraction.md`). This one
is one run old. The rung to confirm it exists now and costs nothing to re-run,
so the change should be made against it deliberately rather than tonight.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The change is to the probe only; no driver code moved.
