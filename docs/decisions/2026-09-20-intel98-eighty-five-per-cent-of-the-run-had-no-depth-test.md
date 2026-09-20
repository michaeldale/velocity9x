# intel98: eighty-five per cent of the run had no depth test

2026-09-20, MICHAEL-NETBOOK (945GSE), build `50b22a2-dirty`, 3DMark99,
`D3dPidDistinct=1`, `CountDriverInit=1`. Attached:
`2026-09-20-intel98-V9XSNAP.txt`, the netbook photograph, and the reference
frame the operator supplied.

## The draw-path work landed on Intel

| | intel97 (`cf1c056`) | intel98 (`50b22a2`) |
|---|---|---|
| draws submitted | 33,473 | **224,838** |
| textured | 3,492 (10.4%) | **194,644 (86.6%)** |
| `DrawsNoHandle` | 29,999 (90%) | 30,202 (13.4%) |
| `IndexedDrawn` / `IndexedCalls` | stub | 18,271 / 18,271 |
| `OnePrimDrawn` / `OnePrimCalls` | 0 / 852 | 591 / 591 |
| triangles | - | 11,512,201 |
| refusals | - | `BatchesEngineRefused=8`, `TrianglesDeclined=0` |

Eight refusals in a run of eleven and a half million triangles.

## And the photograph says what is still wrong

The blue-and-orange guard rail renders correctly - texture, colour, the
orange triangles, geometry and perspective all match the reference frame's
rail. Everything else is missing: no sky, no buildings, no road surface, no
vehicles, no HUD. The background is the cleared buffer.

```
I9xxDepthDraws=32769   I9xxDepthSkipped=192069   I9xxDepthLastFunc=4
```

`4` is `D3DCMP_LESSEQUAL`. The engine emitted `COMPAREFUNC_LESS` and nothing
else, and skipped the depth test outright for any other function:

```c
if (context->z_func != V9X_D3DCMP_LESS) {
    ++i9xx_depth_skipped;
    return 0;
}
```

3DMark99 asks for LESSEQUAL - the near-universal choice - so **192,069 of
224,838 draws went to the ring with no depth test at all.** Without one,
geometry paints in submission order and whatever is drawn late covers what
came before, which is what a scene reduced to a single object looks like.

The counter that named it, `i9xx_depth_last_func`, already existed. No new
instrument was needed, only reading it.

## The hardware was never the limit

S6 bits 18:16 hold a three-bit comparison and the part implements all eight.
The driver named one. The mapping is now in `src\common\i9xx_depth.c`,
transcribed from Mesa 20.3.5's `i915_reg.h` and `i915_state_inlines.h` -
the same source `intel_gen3_3d.h` already cites for `COMPAREFUNC_LESS`
being 2 - and every arm is host-tested.

Seven of the eight D3DCMP values map to their own number and the eighth
does not: `D3DCMP_ALWAYS` is 8 where `COMPAREFUNC_ALWAYS` is 0. That is
exactly the shape of table someone simplifies into arithmetic after checking
three entries, so all eight are asserted, along with the refusal of a value
outside the range. A function this driver does not recognise is still
skipped rather than guessed at; Mesa's default arm returns ALWAYS because
its input is a validated enumeration, and a HAL's input is whatever an
application put in a render state.

## What the change touched, and what it did not

The comparison now travels from the render state to the S6 word:
`v9x_d3d_i9xx_bind_depth_surface` resolves it,
`v9x_i9xx_build_runtime_state` takes it, and the decoder checks the stream
against what the engine declared - the same two-opinions rule every other
field here follows, and it earned its place immediately by rejecting a host
test that had not been updated to declare it.

The generated Phase 4 and Phase 5 scenes pass `COMPAREFUNC_LESS` explicitly,
so their artefacts and CRCs are unchanged: `1229FE1F` before and after.

`dwZCmpCaps` published `LESS` alone and now publishes all eight. That is not
the advertise-then-ignore pattern this file warns about elsewhere, because
the mapping behind it is tested rather than asserted.

## Unverified

None of this has run on the netbook. What is established is that the
comparison 3DMark99 asks for is now emitted rather than discarded; whether
the scene fills in is the next capture's question, and the missing sky, road
and buildings may have more than one cause.
