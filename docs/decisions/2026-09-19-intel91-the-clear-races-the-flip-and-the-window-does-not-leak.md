# intel91: the clear races the flip, the window does not leak, and the underrun repeats

2026-09-19, MICHAEL-NETBOOK (945GSE), build `d4d4010-dirty` with the
watermark reads, Final Reality Robots on hardware Direct3D. Attached:
`2026-09-19-intel91-V9XSNAP.txt`.

## The underrun repeats

```
PipestatCleared=1
PipestatBFirst=0x00000202   PipestatBOr=0x80000202
PipestatAFirst=0x00000203   PipestatAOr=0x00000203
```

Identical to intel90 on a second boot, with the per-session reset now in
the build: bit 31 clear at the baseline on the live pipe, set afterwards.
The scanout underruns, reproducibly.

## Self-refresh is not the cause

```
FwBlc=0x03060106   FwBlc2=0x00000306   FwBlcSelf=0x0000007F
```

Bit 15 of FW_BLC_SELF is the 945-only self-refresh enable and it is CLEAR.
Self-refresh lets the display stop fetching entirely and was the obvious
way to starve a FIFO; it is off, so it is not this.

**The watermark values are NOT decoded here and should not be read as
though they were.** `i945_wm_info` is established - fifo_size 127, max_wm
0x3f, default_wm 1, guard_size 2, cacheline_size 64 - but the FW_BLC bit
layout is not in `i9xx_wm.c`, and a reading of which direction means more
margin came back contradicting the one this author expected. Decoding
0x03060106 from recollection is exactly the mistake this project keeps
paying for. The raw values are recorded and the question is left open.

Two ways to settle it without a databook, both better than a bit decode:
read the same registers under the stock Intel driver in the same mode and
compare, or read them across two different modes and see whether the VBE
BIOS changes them at all. If the BIOS never programs them per mode, the
game's 640x480 runs on whatever the last mode left, which needs no
interpretation of the fields to be a defect.

## The clear races the flip. The Lock never does.

```
BltFlipPending=1001   CountBlt=1030
LockFlipPending=0     CountLock=55791
```

With the two counted apart, intel90's 1,104 resolves entirely to Blt.
**Ninety-seven per cent of Blts arrive with a flip still pending**, and not
one of 55,791 Locks does.

A Blt is how the game clears. So the clear is issued while a flip is
outstanding, on a path that - unlike the Intel draw path since intel78 -
waits for the engine but never for the flip.

What this does NOT establish is harm. The hazard is a clear landing in the
buffer the panel is still fetching, and which buffer the Blt targets
relative to what is displayed is not measured: `DrawsToFront` compares
against the plane base register, which intel89 established holds the
PENDING value, so it is blind for exactly this window. Exposure is
measured; consequence is not.

It is also a difference between the two cards rather than a shared cause.
The Trio3D read `BltFlipPending=0` over 520 Blts - though with Lock unsampled
and on a different present path - so this is an Intel finding, not an
explanation of the S3 symptom.

## The window does not leak, and the guard revert was right

```
FlipIssueLineMin=155  FlipIssueLineMax=556  FlipIssueVactive=576
FlipIssueDeltaMax=1
```

With the guard back to eight lines the window is `line < 568`, and with the
untested `FlipToGDISurface` flips now excluded the highest issue line is
**556 - inside the window**. `FlipIssueDeltaMax` is one scanline again, so
the write path costs a line and no more.

intel89's "the guard leaks by eighty-four lines" is therefore fully
withdrawn: it was two populations in one statistic, and the twenty-odd
desktop-restore flips that skip the test supplied the 660. Nothing leaks,
the eight-line guard is adequate, and the ninety-six-line widening was
cost without benefit.

## Where this leaves the Intel side

The underrun is the only mechanism on the table that predicts the symptom's
shape and burstiness, it reproduces, and self-refresh is excluded as its
cause. The watermark configuration is the next thing to look at and the
values need a comparison rather than a decode.

The Blt exposure is a real defect independent of that and worth closing on
its own terms - the clear should wait for a pending flip as the draws do -
but it explains nothing until the buffer it targets is known, and the
instrument that would say is blind in this window.

The experiment that would settle the underrun outright still needs no
register knowledge: hold a known image on screen and render heavily into a
disjoint buffer without flipping. Flicker with the flip path removed
entirely implicates bandwidth, and this build's PIPESTAT reading would say
so in the same run.
