# Intel runtime Direct3D and the Intel flip are on by default

Date: 2026-09-18
Machine: MICHAEL-NETBOOK, Intel GMA 950 on 945GSE (8086:27AE), Windows 98 SE.
Decision: Michael Dale's, asked for in so many words after intel66.
Status: a decision about defaults, not a measurement. The evidence it rests
on is the eight boots below; what it changes is which way an absent key
reads.

## What changes

`IntelRuntime3D` and `IntelFlip` in `C:\V9XDIAG\INTELARM.TXT` were
permissions: absent or anything but `1` meant off. From this build they are
switches: `0` means off, and absent - which is how a fresh install and
`V9XCOPY` leave the file - means on. The 16-bit side reads them at the same
point as before, still before the arm transaction, and an armed diagnostic
boot still clears both in memory because it owns the ring.

`V9X3D ON` writes runtime on and flip off; `V9X3D FLIP` writes both on,
which is now also the default; `V9X3D OFF` writes both off and remains the
one-line recovery from DOS. `V9XCOPY` now turns both back on as a side
effect of resetting the arm file, where it used to turn them off; the batch
says so.

## Why it was off, and why that reason has been spent

The runtime permission was made absent-means-off on 2026-09-16 because no
machine had executed application geometry on this engine, and a package
that turned the ring on by being installed would take that decision on the
operator's behalf with nothing to reverse it
(`2026-09-15-intel-phase5-errata-gate.md`, sustained-3D amendment). The
flip permission followed the same shape on 2026-09-17 because the plane-base
write and the line-register read had never been made on the part.

Both have now been made, on every boot from intel59 to intel66:

| Boot | Runtime 3D | Flip | Outcome |
|---|---|---|---|
| intel59 | on | off | textured formats accepted, drew untextured (caps) |
| intel60 | on | off | Final Reality textured |
| intel61 | on | off | 239,970 draws textured; pipe B line register sweeps |
| intel62 | on | on | 591 flips handled; first plane-base write, display stayed up |
| intel63 | on | on | 3DMark99; a pending flip that never completed (fixed) |
| intel64 | on | on | 738 flips, none declined or abandoned |
| intel65 | on | on | 2,890 flips, none declined or abandoned |
| intel66 | on | on | 710 flips; in-blank write made tearing worse (reverted) |

No boot in that run hung on the runtime path or the flip. The two hangs of
the period, intel57 and intel58, were the scanout watch reading a
powered-down pipe, established by intel59 running without it; the watch now
reads only an enabled pipe.

## What the decision does not claim

- That the flip is right. intel65 and intel66 show tearing whose cause is
  not established; the frame-tick measurement in the scanout watch is the
  next step and the flip stays at intel65's behaviour meanwhile. A default
  that tears is a default that shows the picture; the alternative, declining
  every Flip, is DirectDraw's CPU copy, which also tears and is slower.
- That the errata gate's reasoning is answered. The sustained-3D amendment
  authorised application draws on this engine on the operator's decision;
  this is the same operator extending that decision to a fresh install. The
  recovery path the amendment required - one line from DOS - is unchanged.
- That any other machine is covered. One 945GSE has run this. The INF
  claims 27AE only, and the two Function 1 IDs claimed by decision on
  2026-09-17 have not booted with it.

## Sites touched

`src\display16\intel_boot16.c` reads both keys with the inverted default;
`src\chipsets\intel\gma950\gma950_hw16.c` stamps the caps as before;
`src\display32\engines\i9xx_scanout.c` and `packaging\win98se\V9X3D.BAT`
carry the corrected words. check-tree's assertion that the runtime key is
read before the arm transaction still holds.
