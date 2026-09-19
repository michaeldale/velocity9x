# Strips and long lists: six million triangles, and an unmoved score

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX), 3DMark 99 Max Pro, full suite,
`D3dPidDistinct=1`. Attached: `2026-09-20-virge-strips-V9XSNA10.txt`.

## The breakdown that decided what to write

The previous run split `IndexedRefusedShape` and the answer had no ambiguity
in it:

```
IndexedPrimTypeSeen=0x00000030   (TRIANGLELIST and TRIANGLESTRIP, nothing else)
IndexedRefusedPrimType=44952     (every refusal)
IndexedRefusedVertType=0  IndexedRefusedCount=0  IndexedRefusedNull=0

OnePrimCalls=852  OnePrimDrawn=0  OnePrimRefusedCount=852
OnePrimPrimTypeSeen=0x00000010   (always a TRIANGLELIST)
OnePrimCountLast=1260            (420 triangles in one refused call)
```

`DrawOnePrimitive` served **none of 852 calls**, because it required a list
to be exactly three vertices. The count was never a property of the shape.

## Both paths now serve everything asked of them

```
IndexedCalls=12989   IndexedDrawn=11635   IndexedRefusedShape=0
IndexedRefusedIndex=0                     IndexedTriangles=3202372
OnePrimCalls=271     OnePrimDrawn=271     OnePrimRefusedCount=0
                                          OnePrimTriangles=2963380
```

Zero refusals of any kind on either path, and `IndexedRefusedIndex=0` again:
no batch was malformed, so the range check costs nothing and the pool
arithmetic holds for strips as it did for lists.

**6.17 million triangles are now drawn** where the previous build drew
227,306 and the build before that drew none of them.

The call counts fell - 49,316 indexed calls to 12,989, 852 single to 271 -
which is what a fixed-duration benchmark does when each frame costs more:
fewer frames, fewer calls, far more geometry per call.

## And the score did not move

207 3DMarks before, **208** after. CPU 3DMarks 1770 to 1799.

Twenty-seven times the triangles for one point. That is not a result to be
pleased about; it is one that does not fit. A renderer given twenty-seven
times the work should either take much longer per frame - which the falling
call counts say it did - or score very differently. Both cannot be true
unless the score is dominated by something other than what these two paths
feed, and nothing here establishes that.

## What is not established

**Whether the picture is right.** A mid-run screenshot showed a dithered
green and blue wedge on black. That may be an ordinary frame of a test this
capture does not identify, caught between draws; it may be the strip
conversion producing wrong geometry. One frame of an unknown scene is not
evidence either way, and the counters cannot tell a correct triangle from a
wrongly-wound one.

The strip conversion emits N-2 triangles for N indices and swaps the first
two vertices on odd triangles, which is D3D's convention.
`V9X_D3DPT_TRIANGLESTRIP` was taken from the DDK's `D3DTYPES.H` rather than
from memory. The code reads correctly, and this project's rule is that
reading correctly is not the same as working.

## What would settle it

The two-VM topology exists for exactly this: `Win98SE-Native-S3` on port
9870 runs the same chip under S3's own driver. The same benchmark, the same
test, the same scene, photographed on both, is a direct comparison that no
counter can substitute for.

Until that is done, what is measured is that the driver now accepts and
submits the geometry it used to discard, and what is not measured is whether
the result looks like the scene.
