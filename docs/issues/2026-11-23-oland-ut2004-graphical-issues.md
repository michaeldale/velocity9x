# UT2004 shows graphical issues on the Oland (W9QI0DAD)

Date: 2026-11-23. Machine: W9QI0DAD, Radeon Oland `1002:6610` (Michael
calls it an R7 240; the subsystem `1028:2120` is a Dell OEM card, so the
retail name is not settled), Windows 98 SE, DirectX 9.0c. Program: Unreal
Tournament 2004 demo, build 3334, in `C:\UT2004Demo`. Status: open,
deferred to 1.1. Nothing measured.

## What is known

- Michael reported graphical issues in UT2004 on this card. The symptoms,
  map, mode, colour depth and driver build were not described when this
  was recorded; they are to be taken when it is tested.
- The only recorded run here is 2026-11-20, boot 123, on the GCN Z24
  build (`7ad8b7e5-dirty`)
  ([decision](../decisions/2026-11-20-gcn-z24-depth.md)): 800x600
  X8R8G8B8 + D24X8 through Direct3D 8, a DM-Rankin botmatch to a score
  (28.36), no failed Set or Draw in the d3d8 proxy log, depth ordering
  right in the one OBS frame taken. That record did not look for, and
  does not exclude, the issues reported here.
- Known Oland faults that may bear on it, none linked by evidence: V9XD32
  T19 FfSpecFog mismatch
  ([issue](2026-11-17-t19-ffspecfog-mismatch-oland-gt218.md)); texture
  shapes never judged in a pixel check: non-square DXT chains, DXT
  cubes, a cube's smaller levels, V8U8
  ([issue](2026-11-17-oland-texture-shapes-unmeasured.md)). UT2004 asks
  for DXT5 cube maps on its first frame (the HD 4600 record of
  2026-11-13).

## To do when tested (1.1)

Michael's description of what is wrong, and where; a botmatch with OBS
frames on the current build; the d3d8 proxy log and `V9XPS.LOG` for the
run; the same scene on the HD 4600 for comparison.

## 2026-11-23: UT2004 faults when closed mid-benchmark

Seen while testing B8 on `a92e01f3-dirty` plus the DrawPrimitives2
owner check (boots 131 and 132). UT2004 windowed, a `dm-rankin`
botmatch with `-benchmark -seconds=150`, then its window closed by
`WM_CLOSE` (the agent's `close-window`) about 110 s in: UT2004 shows
its own Critical Error, "General protection fault!", history
`CMainLoopDtor <- MainLoop <- FMallocWindows::Free <-
FMallocWindows::Realloc <- 676F4C57 0 FArray <- FArray::Realloc <-
0*2 <- FMallocWindows::Free`. The same with no other program running
(the control), so it is not the probes beside it. UT2004 also had not
exited on its own 240 s after `-seconds=150` ran out. Whether the
driver has any part in it is not known; not seen when closed from its
menu on 2026-11-20. Evidence:
`docs/probe/1.0-b8-fix-oland-2026-11-23/` (`ut-after-close.png`,
`control/after-close.png`).

## 1.0 re-read, 2026-11-26

Moved after 1.0: listed in the 1.0 plan's "Not in 1.0". Already listed
under "To test for 1.1" in the 1.0 plan.
