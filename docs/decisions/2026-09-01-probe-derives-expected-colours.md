# The probe now asks the surface what its colours are, and six ViRGE keys go red

Date: 2026-09-01
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2, work-order step 10.

## What changed and why

Every "did it draw the right colour" verdict in `tools\diag\ddraw_probe_win32.c`
compared against a literal: `0x7C00` red, `0x03E0` green, `0x7FFF` white,
`0x400F` for the alpha blend, and masked field ranges for the trilinear and
ARGB4444 checks. Those are ZRGB1555. The render target is RGB565.

The literals were written to match what the ViRGE's triangle engine writes
rather than what the surface declares, and that was harmless while one engine
existed. It stopped being harmless the moment a second one arrived: on
2026-09-01 the software engine passed the flat triangle, the depth ladder and
the depth write mask while all three keys reported 0, and each had to be read
back out of its raw value by hand. Every capability added after that would have
joined them - which, with textures next, was about to be four more.

The probe now reads the render target's `ddpfPixelFormat` and builds its
expected colours from the masks. Channels are compared as 0..255 where a range
is wanted, so a tolerance written once means the same thing in 1555 and in 565,
where green has a different field width. The old literals remain as the
fallback for a surface that will not describe itself, and the result file now
records what was used:

```
D3DTargetFormatValid=1   D3DTargetRMask=63488  D3DTargetGMask=2016
D3DTargetBMask=31        D3DExpectRed=63488    D3DExpectGreen=2016
D3DExpectBlue=31         D3DExpectWhite=65535
```

Those masks are RGB565, on both guests, queried rather than assumed.

## What it measured

Same binary on both, `Build=4472955-dirty`. Guests `Win98SE-Trio64` (port 9871,
`Direct3D=2`) and `Win86SE` (port 9869, hardware).

**Six keys on the Trio64 turned from 0 to 1 with no driver change:**
`D3DTrianglePixelOk`, `D3DSubpixelTriangleOk`, `D3DZCompareOk`,
`D3DZWriteMaskOk`, `D3DSpecularGouraudOk`, alongside `D3DDepthFogOk` and
`D3DTriangleShapeOk` which already passed. The software engine draws flat and
subpixel triangles, tests depth through all four rungs of the compare ladder,
honours the depth write mask, applies specular Gouraud and applies depth fog -
and every one of those was true before this change and invisible.

Specular is the one that was not merely hidden but unnoticed: the core folds
the specular contribution into the vertex colour before the engine sees it, so
the software engine got it without any code being written for it.

**The same six keys on the ViRGE turned from 1 to 0**, and that is a real
defect rather than a broken probe - see
[the issue](../issues/2026-09-01-virge-3d-writes-zrgb1555.md). The pattern is
what proves it: every ViRGE key whose expected colour is blue still passes,
because `0x001F` is blue in both formats and is the one colour they agree on,
while every key carrying red, green or white fails.
`D3DVertexAlphaBlendRaw=16399` is `0x400F` - exactly the literal the probe used
to compare against, written unchanged into a 565 surface.

## The cost of the change, stated plainly

The ViRGE's Direct3D ladder is no longer green, and it has been the release
gate for three chips. That is the intended consequence and it was agreed before
the change was made: the alternative was a probe that certifies one engine's
bug as the standard and reports every other engine's correctness as a failure.

Nothing about the ViRGE's rendering changed. What changed is that its known
defect is now measured from outside instead of recorded in a comment, and the
issue names the two hypotheses and the machine that can distinguish them.
