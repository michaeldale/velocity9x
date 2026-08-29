# Four people shipped GL on the ViRGE and none of them wrote an ICD; the two walls are triangle setup and multiplicative blend

Date: 2026-08-30
Branch: `d3d-zbuffer`

Instrument: a single archived vendor page, read and not otherwise corroborated:
<https://ctrl-alt-rees.com/archive/s3.dimension3d.com/quakevirge.htm> (S3's
own `dimension3d.com` GLQuake page, circa 1998, via the ctrl-alt-rees archive).

This is a **prior-art note**, not a measurement. Nothing here was reproduced on
a card or a guest. It is recorded because it constrains two pieces of work —
the OpenGL question that prompted it, and the D3D blend paths already in the
tree — and because a contemporaneous vendor statement about a chip is worth
more than our inference from the databook.

## What the page is worth, and what it is not

It is S3 writing about S3 silicon while that silicon was current, so on the
question "what could the engine not do" it is close to first-party. It is also
marketing-adjacent, on a machine we do not have, with a workload we are not
building, and the numbers below are their measurements taken on their bench.
Treat the *constraints* as strong evidence and the *figures* as an order of
magnitude only.

## Measured, by them

A K6-2/300, 64 MB, 4 MB ViRGE **GX2**:

| Mode | Reported |
|---|---|
| 320x240, bilinear, S3 pre-pipeline wrapper | ~55 fps |
| 640x480 | ~22 fps |

The plain ViRGE **325** is described as essentially unplayable for GLQuake.
Whatever the exact numbers, the useful shape is that the family spans from
unusable to adequate depending on the part, so any acceleration claim we make
has to name the chip.

## The two hardware walls

**No triangle setup.** The page states it plainly: the ViRGE family has none.
The CPU computes every gradient and writes the per-triangle setup registers,
so the host is the bottleneck and the interesting optimisation lives in
host-side C, not in register poking. This matches the layering rule — the
mechanics belong behind the backend, the per-triangle arithmetic is pure,
testable policy.

**No multiplicative blending.** The engine cannot do it, which is why Quake 2
lightmaps were impossible on the part; the documented workarounds were vertex
lighting or a Streams Processor trick. This one is not confined to the OpenGL
question. **It applies to the D3D HAL already in the tree**: the first title
that asks for a `D3DBLEND_DESTCOLOR`-style modulate will hit the same silicon
limit, and we currently have no recorded position on what the HAL does when it
does. That is an open question, not a finding.

## What this kills

The framing I gave before reading it — that OpenGL on the ViRGE means porting
Mesa as a full Win9x ICD — is contradicted by every attempt that actually
shipped. The page names four, and none is a conformant ICD:

- an S3 wrapper with a geometry pipeline, added to sidestep DirectX 5 retained
  mode overhead (not a path we would take: we do not go through retained mode);
- a second S3 wrapper supporting bilinear filtering;
- Techland's native **MiniGL** driver;
- **S3MESA**, described as an OpenGL clone;
- a hacked i740 wrapper.

So the achievable deliverable is a MiniGL — the small subset of entry points
GLQuake actually calls — or a wrapper onto our own D3D/S3D path, not an
OpenGL 1.1 implementation. That is a materially smaller piece of work.

The page also mentions NT4 Mini Client Driver support, which reinforces the
separate point that MCD is an NT-only driver model: on 95/98 there is no
rasterisation-level hook to sit in, so a 9x MiniGL has to be its own DLL.

## Standing

No work is proposed off the back of this. The ordering stated when the question
was asked is unchanged: the DirectDraw/D3D path and the Z-buffer on
`d3d-zbuffer` are still unvalidated on hardware, and a GL layer would sit on
top of that same unproven submission code. This note exists so that when the
question is picked up, the four dead ends above are not walked again.

The one item that is live now is the blend limitation, since the HAL is already
shipping blend states.
