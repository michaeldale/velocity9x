# The Trio3D runs the matrix: every cell but the blended ones, and no 3D-done bit

Date: 2026-09-04
Status: measured on A8U4I5 (S3 Trio3D/2X, 5333:8A13); 90 of 117 cells, and the
27 failures are exactly the 27 blended cells

## What was run

The pair at 6797d93 was pushed to A8U4I5 over the WININIT.INI rename route -
`V9XDISP.DRV` 41,256 -> 41,576 and `V9XHAL.DLL` 41,472 -> 41,984, both
confirmed by size after the warm restart. `V9XSETP.DLL` and `V9XMINI.VXD` are
unchanged at HEAD and were left alone. The probe is `V9XDDP.EXE` 49,664, the
same binary that produced
`docs/probe/references/virge-dx-86box-v9x-2026-09-03b.ini` on the emulated
ViRGE/DX.

Guest: Windows 98 SE, 800x600x16, `ColourLayout=555-auto`,
`Direct3DMode=hardware`, `VideoMemoryBytes=4194304`, boot 22. `Result=COMPLETE`
in 885 ms of measured work (`TexMatrixMs=825`, `TargetsMs=60`). The capture is
`docs/probe/references/trio3d-a8u4i5-v9x-2026-09-04.ini`.

## The matrix: 90 of 117, and which 27

```
TexMatrixOk=90 TexMatrixCount=117 TexMatrixCountsOk=1
```

Every cell that does not blend passes: NEAREST, LINEAR, MIPNEAREST,
LINEARMIPLINEAR and wrap, at 64, 128 and 256 texels, in ARGB1555 and ARGB4444,
plain and DirectDraw-built chain and hand-built gapped chain. Ninety of ninety.
The wrap cells are 18 of 18 and the gapped chains carry a real gap
(`MipGapDelta=8192`).

The 27 failures are the whole of the blended set and nothing else: the `alpha`
cell at all three sizes, both formats, all three layouts (18), and the `halfa`
cell at all three sizes and three layouts in ARGB4444 (9).

In every `alpha` cell the reading is the same:

```
TexM_<size>_<fmt>_<layout>_alpha_L=0     the emulator reads 992 (green)
TexM_<size>_<fmt>_<layout>_alpha_R=0     correct: those texels have alpha 0
```

The half whose texels are opaque draws nothing. The half whose texels have
alpha 0 correctly draws nothing. Turning `ALPHABLENDENABLE` on removes the
fragment that should have survived it, and the texture unit is not at fault:
the same texture, same size, same layout, same filter, with the blend off, is
right in all 90 of the other cells.

`halfa` is not the same reading. Its opaque left half is also 0, but its right
half - one ARGB4444 texel of alpha 8 of 15, blue - reads 23254 (`0x5AD6`:
r22 g22 b22 of 31, a neutral 176 of 255), where the emulator reads 16
(`0x0010`: blue at 128 of 255). So the part is not simply discarding blended
fragments; something is drawn, in all three channels, from a texel that has
colour in one.

## Beside each cell: nothing refused, nothing skipped, nothing faulted

`TexMatrixCountsOk=1`, so the display driver answered `V9X_DDGETCOUNTS` and the
absence of a delta key means the counter did not move. No `_Dref`, no `_Dskip`,
no `_Dfault` was written anywhere in the run: no texture was refused, no blend
was skipped by the driver, and no engine FIFO timeout, idle timeout or reset
occurred. The blended cells were issued to the hardware and came back wrong,
not declined by us.

## The 3D-done bit is not on this card

The only delta key written in the entire run is `_Dmiss`, and it is written for
every cell:

```
115 cells                                  _Dmiss=2
Tgt_320, Tgt_640                           _Dmiss=3
TexM_256_1555_chain_trilin,
TexM_256_4444_chain_trilin                 _Dmiss=4
```

Every wait that asked for SUBSYS_STAT bit 1 timed out and accepted idle
without it. This is the counter
`docs/decisions/2026-09-03-the-probe-matrix-and-the-3d-done-bit.md` named as
the first thing to read here, and the answer is that the Trio3D/2X never sets
the bit the way 86Box's ViRGE model does. The degradation is doing its job -
the wait falls back to the idle bit, no reset is provoked, and the 90 passing
cells are stable - but nothing on this part is being gated by the done bit.
Whether the part has the bit elsewhere, or has none, this run does not say.

## Render targets of real sizes

320x240 and 640x480 pass all three depth steps with pitches 640 and 1280, the
same as the emulator. 800x600 and 1024x768 are refused by DirectDraw at
CreateSurface with `E_INVALIDARG` (`Tgt_800_TargetHr=0x80070057`,
`Tgt_1024_TargetHr` the same), before the driver is asked - again the same as
the emulator, and so not a property of this card.

## What this disputes

`docs/issues/2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md` records
mip level selection as a Trio3D-versus-ViRGE/DX difference. It is not one now:

```
                       Trio3D    86Box ViRGE/DX
D3DMipmapLevelRaw        992          992
D3DMipmapLevelSelectOk     0            0
D3DTrilinearRaw          992          992
D3DTrilinearBlendOk        0            0
```

Identical, and every `mipnear` and `trilin` cell in the matrix passes on the
card at every size, format and layout. Those two keys remain open on both
machines - the level-1 fetch is a shared question, not a chip difference. The
earlier readings were taken with the wrong texture stride on every mipmapped
draw, as
`docs/decisions/2026-09-03-the-trio3d-reads-the-texture-stride.md` warned.

The stride fix itself holds on silicon across the whole space: 128- and
256-texel textures are correct under every filter and both formats, which the
one 128/256 rung had only shown at NEAREST.

Alpha is the only remaining difference between this card and the emulated
ViRGE/DX in anything the probe measures.

## `D3DVertexAlphaBlendRaw` moved again

Against the card's own 2026-09-03 capture, exactly one pre-existing key
differs in the whole file: `D3DVertexAlphaBlendRaw`, 527 -> 25352. The
committed 2026-09-03 reference reads 527 at 800x600x16; the issue's table
records 25352 for a 2026-09-03 boot and 527 for a 2026-09-02 one. Three
captures, two values, the same desktop layout for at least two of them. That is
the direction the issue's second Next item points - a value that wanders is a
read of something uninitialised - but today's run also changed the driver, so
it is not the controlled repeat that item asks for.

## Gates

check-tree. No code changed: this is a measurement and its record.

## Open

- Why an opaque texel draws nothing under `ALPHABLENDENABLE` on this part, and
  what the grey 0x5AD6 in the `halfa` right half is made of. The blend is
  reaching the hardware - the driver refused nothing and skipped nothing.
- Whether the Trio3D/2X signals 3D-done anywhere. `_Dmiss` on every wait, for
  the whole run.
- The controlled `D3DVertexAlphaBlendRaw` repeat: several boots, one driver.
