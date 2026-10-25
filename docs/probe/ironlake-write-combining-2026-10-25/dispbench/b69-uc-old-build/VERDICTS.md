# Verdicts: w9qi2f4f-ilk-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x32, build 0af3316, driver Velocity9x Intel HD Graphics (Ironlake)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 32.2 |
| FbRead | 28.4 |
| MemFill | 21,583.8 |
| MemCopy | 11,714.3 |
| DibToDibBlt | 8,706.1 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 36 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 1,441.7 | 8.485/px | - | 10.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 3,224.1 | 3.794/px | - | 10.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 5,483.2 | 2.231/px | - | 10.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 2,652.5 | 4.612/px | - | 10.9 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,029.9 | 6.026/px | - | 1.9 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 5,578.5 | 2.193/px | - | 1.9 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU | 6,317.4 | 1.936/px | - | 1.9 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 5,178.6 | 2.362/px | - | 1.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,900.8 | 6.436/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 8,350.2 | 1.465/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 9,110.3 | 1.343/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 7,045.5 | 1.736/px | - | 1.1 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 34.4 | 355.422/px | 32.2 | 1,111.3 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 36.7 | 333.045/px | 32.2 | 1,111.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 74.1 | 164.987/px | 32.2 | 1,111.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 36.5 | 334.909/px | 32.2 | 1,111.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 59.6 | 205.338/px | 32.2 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 64.2 | 190.491/px | 32.2 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 64.3 | 190.126/px | 32.2 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 64.4 | 189.874/px | 32.2 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 58.2 | 210.308/px | 32.2 | 5.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 64.5 | 189.693/px | 32.2 | 5.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 64.7 | 189.185/px | 32.2 | 5.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 63.9 | 191.511/px | 32.2 | 5.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 27.4 | 445.964/px | 28.4 | 5.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 27.9 | 438.674/px | 28.4 | 5.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 27.9 | 438.012/px | 28.4 | 5.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 27.8 | 439.690/px | 28.4 | 5.2 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 53.4 | 228.961/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 55.9 | 218.987/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 55.9 | 218.693/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 55.8 | 219.396/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 99.4 | 123.057/px | 28.4 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 111.6 | 109.618/px | 28.4 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 111.9 | 109.350/px | 28.4 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 110.9 | 110.331/px | 28.4 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 16.2 | 757.127/px | 32.2 | 3.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 16.3 | 752.490/px | 32.2 | 3.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 16.3 | 752.257/px | 32.2 | 3.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 16.2 | 755.075/px | 32.2 | 3.1 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 222.2 | 55.054/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 2,778.0 | 4.403/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,864.1 | 4.271/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 2,050.3 | 5.966/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | HW | 38.3 | 319.095/px | 32.2 | 6.9 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | HW | 40.5 | 301.753/px | 32.2 | 6.9 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | HW | 40.6 | 300.938/px | 32.2 | 6.9 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | HW | 40.3 | 303.565/px | 32.2 | 6.9 | D3; D4; not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,332.5 | 5.244/px | - | 1.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 5,591.8 | 2.188/px | - | 1.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU | 6,319.8 | 1.936/px | - | 1.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 5,311.5 | 2.303/px | - | 1.8 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 60.0 | 204.032/px | 32.2 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 64.2 | 190.593/px | 32.2 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 64.3 | 190.152/px | 32.2 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 64.4 | 189.851/px | 32.2 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,388.1 | 5.122/px | 28.4 | 1.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 5,646.2 | 2.167/px | 28.4 | 1.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 6,388.6 | 1.915/px | 28.4 | 1.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 5,793.7 | 2.111/px | 28.4 | 1.7 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 223.8 | 54.665/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 2,778.0 | 4.403/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,864.1 | 4.271/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 2,049.5 | 5.969/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | HW? | 34.5 | 354.539/px | 32.2 | 1,109.7 | D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | HW | 36.8 | 332.735/px | 32.2 | 1,109.7 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 74.2 | 164.953/px | 32.2 | 1,109.7 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | HW | 36.5 | 335.040/px | 32.2 | 1,109.7 | D3; D4; not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,616.0 | 4.676/px | - | 0.7 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 9,766.2 | 1.253/px | - | 0.7 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 10,398.9 | 1.176/px | - | 0.7 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 8,641.1 | 1.416/px | - | 0.7 |  |
| Dd.ColorFill.Vid.16x16 | HW | 239.1 | 51.171/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 3,781.3 | 3.235/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 3,869.0 | 3.162/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 2,803.0 | 4.364/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,283.159/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,283.699/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,290.033/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 961.887/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 49,772,524.000/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 15.8 | 774.947/px | 32.2 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 16.1 | 759.963/px | 32.2 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 16.0 | 762.175/px | 32.2 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 16.0 | 763.695/px | 32.2 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 16.0 | 765.688/px | 32.2 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 15.8 | 773.246/px | 32.2 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 32.5 | 376.933/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 32.2 | 379.730/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 31.2 | 392.056/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 32.4 | 378.044/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 32.1 | 381.259/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 32.4 | 377.003/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 32.4 | 377.756/px | 32.2 | 6.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 29.6 | 413.246/px | 32.2 | 6.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 32.5 | 376.607/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 32.2 | 379.819/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 30.8 | 396.684/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 32.4 | 377.879/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 32.0 | 382.706/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 32.4 | 377.421/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 32.3 | 378.759/px | 32.2 | 4.7 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 28.4 | 431.443/px | 32.2 | 4.7 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 28.3 | 432.488/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 28.1 | 435.184/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 28.2 | 433.473/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 28.2 | 433.427/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 28.1 | 434.694/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 28.3 | 432.437/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 28.3 | 432.754/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 26.4 | 463.962/px | 28.4 | 5.6 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 15.5 | 786.738/px | 32.2 | 0.8 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 15.6 | 783.771/px | 32.2 | 0.8 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 15.6 | 786.256/px | 32.2 | 0.8 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 15.5 | 788.544/px | 32.2 | 0.8 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 15.5 | 786.677/px | 32.2 | 0.8 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 15.0 | 814.233/px | 32.2 | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 15.4 | 796.562/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 15.3 | 800.825/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 15.4 | 795.547/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 15.3 | 800.687/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 15.4 | 795.023/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 15.3 | 799.722/px | 32.2 | 4.4 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 15.6 | 786.156/px | 32.2 | 4.4 |  |
| Gdi.Ellipse.128x128 | CPU | 30.6 | 400.062/px | 32.2 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 1,831,843.673/string | 32.2 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 358,370.751/string | 32.2 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 1,443,708.381/string | 32.2 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 219,224.842/string | 32.2 | - |  |
| Gdi.FillRect.1024x768 | CPU | 32.2 | 379.470/px | 32.2 | 4.0 |  |
| Gdi.FillRect.128x128 | CPU | 32.0 | 382.089/px | 32.2 | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 31.2 | 391.771/px | 32.2 | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 32.2 | 380.326/px | 32.2 | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 32.0 | 381.737/px | 32.2 | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 32.2 | 379.798/px | 32.2 | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 32.2 | 379.423/px | 32.2 | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 28.4 | 430.318/px | 32.2 | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 34,168.952/line | 32.2 | - |  |
| Gdi.LineHorizontal | n/a | - | 40,083.788/line | 32.2 | - |  |
| Gdi.LineVertical | n/a | - | 76,687.056/line | 32.2 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 32.2 | 379.698/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.128x128 | CPU | 32.0 | 382.437/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 30.3 | 403.089/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 32.1 | 380.863/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 31.8 | 384.918/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 32.2 | 379.703/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 32.1 | 381.081/px | 32.2 | 7.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 26.7 | 458.163/px | 32.2 | 7.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 32.2 | 379.831/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.128x128 | CPU | 32.0 | 382.689/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.16x16 | CPU | 30.3 | 403.078/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.256x256 | CPU | 32.1 | 380.859/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.32x32 | CPU | 31.8 | 385.256/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.512x512 | CPU | 32.2 | 380.199/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.64x64 | CPU | 32.1 | 381.400/px | 32.2 | 5.4 |  |
| Gdi.PatBltPattern.8x8 | CPU | 26.7 | 458.192/px | 32.2 | 5.4 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 32.2 | 379.544/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.128x128 | CPU | 32.0 | 381.856/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.16x16 | CPU | 31.9 | 383.663/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.256x256 | CPU | 32.1 | 380.599/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.32x32 | CPU | 32.2 | 379.860/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.512x512 | CPU | 32.2 | 379.688/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.64x64 | CPU | 32.3 | 379.209/px | 32.2 | 4.5 |  |
| Gdi.PatBltSolid.8x8 | CPU | 30.8 | 397.578/px | 32.2 | 4.5 |  |
| Gdi.Polygon.128x128 | CPU | 29.5 | 414.882/px | 32.2 | - |  |
| Gdi.Polyline | n/a | - | 38,438.032/line | 32.2 | - |  |
| Gdi.Rectangle.128x128 | CPU | 31.8 | 384.391/px | 32.2 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 32.3 | 378.615/px | 32.2 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 15.5 | 790.405/px | 32.2 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 32.3 | 378.277/px | 32.2 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 10.0 | 1,227.678/px | 32.2 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 10.0 | 1,222.526/px | 32.2 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 15.6 | 786.611/px | 32.2 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 10.0 | 1,222.040/px | 32.2 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 32.3 | 378.286/px | 32.2 | - |  |
| Gdi.RoundRect.128x128 | CPU | 31.2 | 392.348/px | 32.2 | - |  |
| Gdi.ScrollBitBlt | CPU | 15.3 | 799.743/px | 32.2 | - |  |
| Gdi.ScrollDC | CPU | 15.3 | 799.685/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 32.4 | 377.421/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 32.4 | 377.479/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 32.4 | 377.195/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 32.4 | 377.289/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 32.3 | 379.199/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 32.2 | 379.393/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 32.2 | 379.520/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 32.3 | 379.071/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 32.2 | 380.162/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 32.1 | 380.776/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 32.2 | 380.262/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 32.1 | 380.913/px | 32.2 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 5.5 | 2,204.407/px | 32.2 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 5.5 | 2,227.321/px | 32.2 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 5.6 | 2,202.997/px | 32.2 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 5.5 | 2,227.380/px | 32.2 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 23.6 | 518.675/px | 32.2 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 23.8 | 512.973/px | 32.2 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 23.6 | 518.953/px | 32.2 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 23.9 | 512.232/px | 32.2 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 31.9 | 383.367/px | 32.2 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 32.1 | 381.566/px | 32.2 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 29.3 | 418.073/px | 32.2 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 30.1 | 405.860/px | 32.2 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 1,830,238.694/string | 32.2 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 358,050.972/string | 32.2 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 1,443,205.968/string | 32.2 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 219,023.683/string | 32.2 | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- DBGDI: truncated report
