# The GDI solid fill takes a logical RGB where the engine wants a physical colour

Date: 2026-08-26
Status: **fixed** 2026-08-26 in `gdi-accel-001`
Found by: `V9XGDI.EXE /accel` on the ViRGE/DX guest (`:9869`), build
`gdi-accel-000` with `GdiAccelFill=1` set by hand

## Symptom

A `PATCOPY` solid fill executed by the ViRGE engine paints the wrong colour.
The rectangle's shape and position are correct; only its colour is wrong, and it
is wrong the same way every time - white.

Measured at 640x480x8 on the ViRGE guest:

| | screen | reference DC |
|---|---|---|
| pixel at (105, 230) | 255, 255, 255 (white) | 255, 255, 0 (yellow) |

32897 of 230400 compared bytes differ - about 14 per cent of the compared
region, which is one fill rectangle's worth. Everything else in the run agrees.

## Cause

The driver's own counters name it exactly. From the same run:

```
LastRop256=240          (0xF0 - PATCOPY, correctly classified)
LastBrushFlags=129      (0x81 - COLORSOLID | DIBENGBRUSH)
LastBpp=8
LastColor=16711935      (0x00FF00FF)
```

`0x00FF00FF` is `COLORREF` magenta - R=255, G=0, B=255 - and magenta is one of
the sixteen colours the harness draws with. So the value the dispatcher read out
of the realized brush's `FgColor` and wrote into the ViRGE's `PATTERN_FG` is a
**logical RGB triple**, not a physical palette index.

The ViRGE at 8 bpp takes the low byte of `PATTERN_FG` as the pixel value. The
low byte of `0x00FF00FF` is `0xFF`, and index 0xFF in the Windows 98 default
system palette is white. Which is precisely the white on the screen.

The gates and the classification are all correct. The brush is solid
(`COLORSOLID`), DIBENG realized it (`DIBENGBRUSH`), the ROP is PATCOPY, the
depth is 8. The single wrong thing is the colour's *space*.

`DIBENG.INC:189` labels the field "Physical fg color", which is what the
dispatcher was written against. On this path it is evidently not that - or it is
that only for some brush realizations, and the physical index has to be obtained
another way.

## The fix

It was the third candidate: the physical colour for a solid brush lives in
`Bits`, not `FgColor`. The reference driver says so in first-party code -
`PB_SolidPatBlt` in `98DDK/src/display/mini/s3v/S3BLT.ASM` does

```
        mov     ecx,dword ptr ds:[si.dp8BrushBits] ; ECX = solid foregnd color
        ...
        EngineWrite B_PAT_FG_CLR,  es, ecx ; write ECX = foreground color
```

and the comment on that instruction is the whole answer.

There is a reason it has to be `Bits` and not `FgColor`, worth keeping because
it means no other field could have worked: DIBENG renders `Bits[]` at the
**destination's** depth, so its first DWORD is already the physical value
replicated across the dword - four pixels at 8 bpp, two at 16 - which is exactly
the form a pattern-colour register wants. A single logical RGB could not have
served either depth.

Two things landed with it:

- **The style is checked as well as the flag**, in the reference's order.
  `PatternBlt` branches on `BrushStyle` before it looks at `BrushFlags`, and
  only `BS_SOLID` means a solid colour: `BS_HOLLOW` draws nothing at all.
- **BLACKNESS and WHITENESS stopped being normalised into a colour.** They now
  reach the ViRGE as ROPs in `CMD_SET` bits 24:17, which is what `DoBltNoDSP`
  does, so with an all-ones mono pattern the engine generates the pixel values
  itself and neither chip needs an opinion about what black and white are. The
  Trio64's 8514/A command set has no ROP field, so there they stay a solid fill
  of 0 or of all ones. That retires the open question the original
  normalisation carried.

`BrushBpp` and `BrushStyle` were added to `V9X_GDI_STATS` while diagnosing, and
they confirmed the brush was realized at the screen's depth (`LastBrushBpp=8`,
`LastBrushStyle=0`), which is what ruled the first two candidates out.

## Verified

Randomized comparison against a DIB Engine reference, ViRGE at 640x480x8: 500
operations, 20 comparisons, `Compared=PASS` with `FillsDelta=197` fills executed
on the engine, and the fault-injection step passing on the same run. 16 bpp is
covered by the mode matrix, which runs the same phase in every mode.


## Why this is not a build 000 defect

Nothing ships on it. Every primitive is compiled off at 000, so the shipping
driver declines every blit and the DIB Engine draws the desktop; the mode matrix
passes on all eleven modes on both S3 chips and on all six on the engine-less
`ati` guest with the fill unreachable. The fill was reached only by setting
`GdiAccelFill=1` in `SYSTEM.INI` by hand, which is the deliberate run
`docs/plans/gdi-accel-000-and-harness.md` Block 3 asks for - "purely to prove
the primitive can fire at all before build 001 claims it works."

It fired, and it is wrong. That is the run doing its job. The alternative -
finding this after 001 had been declared working - is what the plan's insistence
on landing the harness first was protecting against, and it is worth recording
that the protection paid for itself on its first use.

## Reproducing it

```powershell
# On the ViRGE guest, with the gdi-accel-000 package installed:
#   append to C:\WINDOWS\SYSTEM.INI
#     [Velocity9x]
#     GdiAccelFill=1
#   reboot, then confirm C:\V9XHW.INI says GdiAcceleration=gdi-fill
#   set an 8- or 16-bpp mode, then:
#     V9XGDI.EXE /accel
#   read C:\V9XACCE.INI
```

`Result=FAIL`, `Error=pixel-mismatch`, and the `Last*` keys carry the diagnosis.
`V9XGDI.EXE /stats` prints the same counters without drawing anything.
