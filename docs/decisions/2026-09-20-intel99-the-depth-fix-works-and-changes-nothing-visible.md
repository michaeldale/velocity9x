# intel99: the depth fix works and changes nothing visible

2026-09-20, MICHAEL-NETBOOK (945GSE), build `b2bb93a-dirty` - HEAD at
`b2bb93a` with the depth work in the tree uncommitted, so the binary carries
it. 3DMark99, `D3dPidDistinct=1`, `CountDriverInit=1`. Attached:
`2026-09-20-intel99-V9XSNAP.txt`.

## The fix landed

```
I9xxDepthDraws=178661   I9xxDepthSkipped=0   I9xxDepthLastFunc=0
```

Every one of 178,661 draws now carries a depth test, against 192,069 skipped
of 224,838 in intel98. The comparison the application asks for is emitted.

## And the operator reports no visible improvement

The scene looks as it did. So the depth skip was **not** the reason the
scene was missing, and the reasoning in the intel98 record - that geometry
without a depth test paints in submission order and late draws cover early
ones - does not survive. That prediction was made from the source and the
photograph and it was wrong.

The fix is still right. Emitting LESS for an application that asked for
LESSEQUAL is a defect whatever else is true, and the mapping is host-tested.
It simply is not this defect.

## What the counters now say, and where they stop

```
I9xxDrawsSubmitted=178661   I9xxTextureDraws=148503   FlipHandled=2796
IndexedTriangles=4431421    OnePrimTriangles=4431420
BatchesEngineRefused=31     TrianglesDeclined=0    DrawsNoHandle=30187
D3dTextureRefusedFormat=0   D3dBlendSkipped=0      DrawsIntoPresented=0
```

Roughly **3,170 triangles a frame across 2,796 frames**, 83 per cent
textured, 31 refusals in the whole run, nothing declined, no texture refused
for any reason, depth accepted, every handle resolving.

By every measurement this driver takes, the geometry is being submitted.
One object appears.

**The counters have reached their limit.** Everything here is upstream of
the framebuffer: they say what was handed to the ring, and nothing says what
reached memory. Three explanations remain and no counter separates them:

- the hardware rejects the fragments - depth, alpha or scissor
- the geometry lands outside the viewport
- it is drawn and then overwritten

`IndexedTriangles` and `OnePrimTriangles` differing by exactly one -
4,431,421 against 4,431,420 - is odd enough to be worth noting and is not
explained.

## What would settle it

Reading the back buffer rather than counting submissions. After a frame, a
count of pixels differing from the clear colour, and their bounding box,
turns "is it drawn" from an inference into a number. The driver already has
the aperture. A scene that submits three thousand triangles and leaves two
hundred pixels changed is a fragment-rejection or a transform problem; one
that changes a full frame's worth and still shows one object is an
overwrite.

The alternative is a probe that draws a known triangle with the same state
3DMark99 sets and checks where it landed, which isolates the state rather
than the scene.

Guessing again would be the third time on this question.
