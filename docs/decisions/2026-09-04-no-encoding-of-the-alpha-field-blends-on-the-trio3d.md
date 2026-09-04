# No encoding of the S3D alpha field blends on the Trio3D/2X

Date: 2026-09-04
Status: measured on A8U4I5, with the instrument validated on the emulated
ViRGE/DX first; the caps question this raises is open

## The question the curve left

The alpha transfer curve
(`2026-09-04-what-the-trio3d-blend-does-with-its-operands.md`) established that
on the Trio3D/2X the source's colour has no effect on a blended draw, that
both of the engine's alpha paths give byte-identical wrong answers, and that
neither endpoint of the blend equation is reached. What it could not say is
what the command word's alpha field - bits 19:18 - actually means on that
part. The engine sets one of two encodings from the blend states; there are
four.

86Box cannot answer it. Its S3D unit has no chip-conditional code at all
(recorded in
`docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md`, Next 1),
so it models one blend for both parts and gets it right for both. Only the card
can.

## The instrument

`V9X_D3DRENDERSTATE_V9X_ALPHAFORCE`: a render state whose magic argument makes
the engine emit a named encoding of those two bits instead of its own choice.
It applies only where a blend was going to happen, so it cannot blend a draw
that asked for none nor revive a blend pair the unit has no expression for, and
it is the last thing applied - after the colour-key path, which also chooses
alpha bits.

**The number matters, and the first one was wrong.** A private state number far
outside Direct3D's range (0x56394146) never reaches the driver:
`IDirect3DDevice2::SetRenderState` validates the type first, so all four forced
curves came back as one flat white - the probe's guard on the failed call had
skipped binding the texture, and an untextured white triangle is what it drew.
The state is now `D3DRENDERSTATE_STIPPLEPATTERN31`, which the runtime passes
through, which this driver publishes no capability for and otherwise ignores,
with `V9X_D3D_ALPHAFORCE_MAGIC` in the argument so an application that does
write a stipple pattern cannot trip it. The probe records each
`SetRenderState`'s HRESULT beside its curve rather than inferring that it took.

## Validated on the emulator first

Emulated ViRGE/DX, `docs/probe/references/virge-dx-86box-v9x-2026-09-04b.ini`:

```
                A=0     2      4      6      8     10     12     14    15
unforced      31744  27652  23560  19468  14352  10261   6169   2077    31
F3 (10)       31744  27652  23560  19468  14352  10261   6169   2077    31
F1 (00)          31     31     31     31     31     31     31     31    31
F2 (01)          31     31     31     31     31     31     31     31    31
F4 (11)          31     31     31     31     31     31     31     31    31
```

All four HRESULTs zero. F3 - `ALPHA_ENABLE` alone, which is what the engine
chooses for a textured blend - reproduces the unforced curve exactly. That is
the self-check: the instrument reaches the command word and changes only what
it says it changes. The other three draw the source opaque, which is what no
blend looks like.

## The card

A8U4I5, `docs/probe/references/trio3d-a8u4i5-v9x-2026-09-04d.ini`, same
binaries, all four HRESULTs zero:

```
F1 (00, no alpha bits)   31 at every step        the source, opaque
F2 (01, ALPHA_SOURCE)    31 at every step        the source, opaque
F3 (10, ALPHA_ENABLE)    0, 4095, 7101, 8026, 8918, 7729, 5483, 2180, 0
F4 (11, both)             0 at every step        nothing drawn at all
```

**None of the four is a blend.** Two draw the fragment opaque, one draws
nothing, and one is the curve already on record - the hump that is a function
of A and the destination's channel position with the source's colour ignored.

F1 is worth stating on its own: with the alpha bits clear the card draws the
textured fragment correctly, in the right colour, over the destination. The
texture path, the fragment colour and the write are all in order. It is only
the two alpha bits that have no working meaning here.

## What this settles, and what it opens

Settled: the Trio3D/2X does not perform a SRCALPHA/INVSRCALPHA blend through
the S3D command word's alpha field under any encoding of it. The remaining
possibilities are that the part enables blending somewhere else entirely - a
register this driver has never written - or that its S3D has no alpha blend.
This probe cannot distinguish those, and no databook here settles it.

Open, and now a decision rather than an investigation: the engine publishes
`D3DPBLENDCAPS_SRCALPHA` and `D3DPSHADECAPS_ALPHA*` for this part, inherited
from the ViRGE/DX descriptor. On this measurement that is advertising what is
not delivered - the pattern this driver has paid for twice. The choices are to
split the descriptor for `5333:8A13` so applications fall back to something
they can draw, or to emit encoding 00 for a blend that cannot be honoured so
the fragment at least appears in its own colours. In 3DMark 99 the second would
turn the black boxes round its sprites into opaque boxes; neither is right, and
which is less wrong is a judgement rather than a measurement. Nothing is
changed here on the strength of one run.

## Gates

check-tree - which now asserts the state number and the magic agree between the
ABI header and the probe, since the probe carries its own vocabulary and a
drifted copy would leave the instrument silently forcing nothing - plus the vga
survey safety gate, host tests and family packages (run-checks).
