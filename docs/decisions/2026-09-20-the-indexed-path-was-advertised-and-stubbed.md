# The indexed path was advertised and stubbed

2026-09-20. intel97 (MICHAEL-NETBOOK, 945GSE, build `cf1c056-dirty`,
`D3dPidDistinct=1`, `CountDriverInit=1`) found it; the 86Box ViRGE guest
verified the fix. Attached: `2026-09-20-virge-indexed-V9XSNA8.txt`.

## What intel97 showed

The DrawPrimitives state fix reached the netbook and works there:
`StateMaxCount=155` where every earlier Intel capture read 0,
`FilterMagSeen` and `FilterMinSeen` both `0x06`, and `DrawsMagLinear` 3,492 -
a counter that exists on this chip, unlike on the ViRGE. 155 is well past the
old 64 cap, so Intel's records were being rejected twice over.

And then the call counts:

```
CountD3dDrawOneIndexed=64251
CountD3dDrawPrimitives=28953
CountD3dDrawOnePrimitive=1148
```

`V9xD3dDrawOneIndexedPrimitive` was six lines that returned
`DRIVER_NOTHANDLED`, while `D3DHAL2_CB32_DRAWONEINDEXEDPRIMITIVE` sat in the
published `dwFlags`. **The driver told the runtime it served indexed
primitives and declined 64,251 of them**, on the path the application used
more than every other path combined.

## The fix, and what it costs

Indices are a WORD array choosing from a vertex pool, so the batch is
gathered into a bounded scratch rather than pointed at - the engines take a
contiguous triangle list and nothing may hand them one the application did
not build. A long list is flushed in pieces rather than refused. Every index
is range-checked against `dwNumVertices` before use, which is the
memory-safety boundary and not a style: a trusted index is an arbitrary read
out of a runtime-supplied pointer.

Verified on the ViRGE guest, full 3DMark99 suite, `D3dPidDistinct=1`:

```
IndexedCalls=49316        IndexedDrawn=3016
IndexedRefusedShape=44952 IndexedRefusedIndex=0
IndexedTriangles=229149
```

**229,149 triangles are now drawn that this driver previously discarded.**
`IndexedRefusedIndex=0` says the range check never fired: no batch was
malformed, so the bound costs nothing and the pool arithmetic is sound.

The score fell from 339 to 207 3DMarks, with CPU 3DMarks unchanged at 1770
against 1764. A 39 per cent drop in the graphics score and none in the CPU
score is what doing 229,149 triangles of previously-skipped work looks like.
The benchmark was faster when it was wrong.

## What is still refused, and why it is not guessed at

`IndexedRefusedShape=44952` - **91 per cent of indexed calls** are still
declined. The test that rejects them checks four things at once: primitive
type, vertex type, a non-empty index array, and an index count that is a
multiple of three. Which of the four fires is not recorded, so which
primitive 3DMark99 is actually asking for is not established.

Triangle strips and fans are the obvious candidates and that is exactly why
they are not being implemented on the strength of it. The counter is being
split first.

## Elsewhere in the same capture

`D3dMipChainChecks` rose from 65,737 to 263,102 and gaps from 34,360 to
220,014, with `MipLevelsMax` 5 to 6. Four times the checks because four times
the draws reach the bind - the ratio is unchanged and the finding of
2026-09-20 stands untouched.

`DrawsHandleUnresolved` is 11, having been 10, then 2, now 11 across four
runs of the same benchmark. It is variance around a small number and remains
unexplained.

## And one still open

`V9xD3dDrawOnePrimitive` is advertised too, and serves only a
`TRIANGLELIST` of exactly three vertices - anything else sets `ddrval` to an
error and draws nothing, uncounted. 1,148 calls on the netbook, so it is
small, but it is the same pattern in the same table.
