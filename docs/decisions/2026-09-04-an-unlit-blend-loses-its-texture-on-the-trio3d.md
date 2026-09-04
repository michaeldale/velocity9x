# A blended UNLIT draw loses its texture colour on the Trio3D/2X

Date: 2026-09-04
Status: measured on A8U4I5 against the emulated ViRGE/DX, fixed and re-measured.
The defect is real and the fix holds; it is **not** what draws 3DMark 99's
black boxes, which are still there.

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

## The fix, made and measured

`V9X_DD_ENGINE_CAP_S3D_UNLIT_ALPHA` says the pairing works on a part;
the ViRGE/DX sets it and the Trio3D/2X does not. Where it is absent and a
textured draw is both blended and not `MODULATE`, the engine emits
`TEXTURE_LIT | TEX_MODULATE` instead of `TEXTURE_UNLIT` and **forces the
Gouraud colour to flat white with zero gradients**. Texel x white is the texel,
so the fragment is the one the application asked for, through the pairing this
card gets right.

The forcing is the part that matters. `DECAL` and `COPY` ignore the vertex
colour by definition, so an application may leave anything in it; modulating by
whatever it left would tint a draw that should not be tinted. White with zero
gradients is the only substitution that is the identity.

It applies only where a blend is actually happening. `TEXTURE_UNLIT` on its own
is correct on every part this engine drives, and that is most of what anything
draws.

Measured, A8U4I5 boot 33 against boot 32, nothing else changed:

```
                     before        after
SpriteOk                  0            1
Spr_*_copy cells    BLACK/red    green/red    all four
Spr_*_mod cells     green/red    green/red    unchanged
TexMatrixOk             108          108
AlphaCurveOk / MipLadderOk / MipTriDegradedOk   1 / 1 / 1 both
```

Emulated ViRGE/DX, same binaries, boot 558: byte-identical to the run before
the change. The substitution does not fire where the capability is set.

In a 3DMark 99 run at 640x480 the census shows the whole population moved:
**25,439 `UNLIT`-with-alpha draws before, zero after**, all of them now
`LIT MODULATE` with alpha. The score is 472 against 475, which is the same
number twice.

## What it did not fix

**3DMark 99's black boxes are still there.** The frame after the change has the
same black rectangle behind the targeting reticle and the same black panel
under the speedometer as the frame before it
(`../images/3dmark99-trio3d-2026-09-04-640x480-race-after-unlit-fix.png`
against `../images/3dmark99-trio3d-2026-09-04-640x480-race.png`).

That is the caveat this document was written with, confirmed: the rung's
failure was the opposite way round from the picture's - opaque going black,
where the picture has a black border round visible art - and the two were never
the same defect. Every blended draw in that run is now `LIT MODULATE`, which is
the pairing the probe says this card handles correctly in all four of its
cells, so whatever draws those rectangles is something the sprite rung still
does not reach. The nine `halfa` cells - partial alpha, the one thing the
matrix still calls wrong on this part - are the next place to look.

The change is kept regardless. It is a real defect, measured on one axis, fixed
at no cost, and confirmed by three independent readings.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks), for the probe and again for the driver change.
