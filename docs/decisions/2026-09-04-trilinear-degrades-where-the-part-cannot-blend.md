# Trilinear degrades to bilinear where the part cannot blend

Date: 2026-09-04
Status: built, gated, and measured on the emulated ViRGE/DX, which is the
control and is unchanged by it. **Not yet measured on A8U4I5**: the machine went
off the network before the run and this is the first thing to do when it is
back.

## What was wrong

`d3d_virge.c` synthesises `D3DFILTER_LINEARMIPLINEAR` from two passes: the
first draws level N with `LINEAR_MIP_NEAREST`, the second re-draws the same
triangle at level N+1 with `ALPHA_SOURCE | ALPHA_ENABLE` and the LOD fraction
as vertex alpha. That is encoding 11 of the command word's alpha field, and on
the Trio3D/2X none of the four encodings of that field is a blend
(`2026-09-04-no-encoding-of-the-alpha-field-blends-on-the-trio3d.md`). The mip
ladder measured what the two passes produce there: one channel at 7 of 31 and
the other two at 22, with the low channel rotating to follow the lower level's
own colour - the alpha defect's signature, three times
(`2026-09-04-the-mip-ladder.md`).

The command-word census says 2,753 of 3DMark 99's 3,775 mip-filtered draws use
`LINMIPLIN`, and that those 3,775 are the only mip usage in the run, which is
why the race scene's noise ground has to be among them
(`2026-09-04-the-command-word-census.md`).

## The change

A chip fact, published where every other chip fact is decided.
`V9X_DD_ENGINE_CAP_S3D_ALPHA` says the part's S3D performs the alpha blend its
command word advertises. The ViRGE/DX's engine function sets it; the Trio3D/2X
now has an engine function of its own - `v9x_trio3d2x_fill_engine` - which is
the ViRGE's with that one bit cleared. The 16-bit side is the authority because
it is what knows which chip it bound.

`v9x_d3d_virge_has_alpha()` reads it from the engine descriptor. Where it is
false, the trilinear path sets `trilinear_degrade` instead of
`trilinear_blend`: the first pass is emitted exactly as before, with
`LINEAR_MIP_NEAREST`, and no second pass follows. That is bilinear filtering on
the level the chip selected - the honest half of trilinear rather than a wrong
whole - and it involves no blend at any point.

It is deliberately the narrow change. The engine still publishes
`D3DPBLENDCAPS_SRCALPHA` for this part and still emits alpha bits for a draw
the application asked to blend; those are the separate decision the alpha
finding raised and this does not pre-empt them. What this bit governs today is
only what the engine *builds* out of a blend for its own purposes.

## Measured: the control

`MipTriDegradedOk` is new, and it exists because `MipTriOk` cannot express
this. `MipTriOk` demands a genuine mix of two levels; a part that degrades
correctly reads exactly one of the two levels' own colours, which is a pass of
a different kind. Both zero is the defect; either one alone is a driver doing
what its chip allows.

Emulated ViRGE/DX, before and after, boot 557
(`docs/probe/references/virge-dx-86box-v9x-2026-09-04d.ini`):

```
MipLadderOk=1  MipTriOk=1  MipTriDegradedOk=0
MipTri_0_Raw (14,17, 0)   MipTri_1_Raw (0,14,17)   MipTri_2_Raw (17,0,31)
```

Identical to the run before the change, to the raw value. The part that has the
capability keeps it and keeps blending; the degrade is not reachable there.

## Not measured: the card

A8U4I5 stopped answering on 10.0.1.172:9869 between the ladder run and this
deployment - no TCP, no ICMP, three attempts. Nothing was asked of it in
between but a screenshot and two reads, and it had been idle for around twenty
minutes, so a power-save sleep is the likeliest reason; it is remote, so waking
it needs someone there.

So the expected reading is written down here before it is taken, which is the
right order anyway:

- `MipLadderOk` stays 1. The degrade does not touch level selection.
- `MipTriOk` goes to 0 and **`MipTriDegradedOk` to 1**, with `MipTri_0_Raw`,
  `_1_` and `_2_` reading exactly red, green and blue - the lower level of each
  pair - instead of the rotating (7,22,22), (22,7,22), (22,22,7).
- The command-word census's `LINMIPLIN` rows disappear from a 3DMark run and
  its `LINMIPNEAR` rows grow by the same number of draws.
- And by eye: whether the race scene's ground stops being chevron noise. That
  is the claim this is really making, and it is the one no key can settle.

If `MipTriDegradedOk` does not come back 1, the degrade is not doing what this
document says and none of the above should be believed.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks).
