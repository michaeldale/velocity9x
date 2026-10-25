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
| MemFill | 21,714.9 |
| MemCopy | 7,630.4 |
| DibToDibBlt | 8,604.4 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 36 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 1,434.9 | 8.525/px | - | 11.1 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 3,221.6 | 3.797/px | - | 11.1 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 5,473.4 | 2.235/px | - | 11.1 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 2,497.9 | 4.897/px | - | 11.1 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,013.3 | 6.076/px | - | 2.0 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 5,567.9 | 2.197/px | - | 2.0 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 6,302.4 | 1.941/px | - | 2.0 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 4,787.2 | 2.555/px | - | 2.0 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,902.1 | 6.431/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 8,349.5 | 1.465/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 9,088.7 | 1.346/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 6,715.6 | 1.822/px | - | 1.1 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 34.4 | 355.128/px | 32.2 | 1,111.2 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 36.7 | 333.218/px | 32.2 | 1,111.2 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 74.1 | 165.143/px | 32.2 | 1,111.2 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 36.5 | 334.737/px | 32.2 | 1,111.2 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 59.7 | 205.011/px | 32.2 | 2.9 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 64.3 | 190.280/px | 32.2 | 2.9 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 64.4 | 189.883/px | 32.2 | 2.9 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 64.4 | 189.953/px | 32.2 | 2.9 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 58.1 | 210.498/px | 32.2 | 4.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 64.6 | 189.480/px | 32.2 | 4.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 64.7 | 189.155/px | 32.2 | 4.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 63.8 | 191.660/px | 32.2 | 4.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 27.5 | 445.534/px | 28.4 | 7.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 27.8 | 440.099/px | 28.4 | 7.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 27.9 | 439.049/px | 28.4 | 7.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 27.7 | 440.943/px | 28.4 | 7.6 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 53.5 | 228.698/px | 28.4 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 55.7 | 219.505/px | 28.4 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 56.0 | 218.371/px | 28.4 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 55.7 | 219.580/px | 28.4 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 99.5 | 122.963/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 111.5 | 109.689/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 111.8 | 109.454/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 110.6 | 110.569/px | 28.4 | 2.5 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 16.2 | 757.259/px | 32.2 | -7.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 16.3 | 751.520/px | 32.2 | -7.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 16.2 | 752.869/px | 32.2 | -7.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 16.2 | 754.758/px | 32.2 | -7.0 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 228.4 | 53.553/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 2,777.5 | 4.404/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,864.0 | 4.271/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 2,050.2 | 5.967/px | 32.2 | 3.4 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | HW | 38.3 | 319.363/px | 32.2 | 4.6 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | HW | 40.6 | 301.602/px | 32.2 | 4.6 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | HW | 40.6 | 301.114/px | 32.2 | 4.6 | D3; D4; not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | HW | 40.4 | 303.091/px | 32.2 | 4.6 | D3; D4; not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,332.6 | 5.244/px | - | 1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 5,581.9 | 2.192/px | - | 1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 6,305.6 | 1.940/px | - | 1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 4,913.9 | 2.489/px | - | 1.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 59.9 | 204.244/px | 32.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 64.2 | 190.457/px | 32.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 64.3 | 190.115/px | 32.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 64.4 | 189.860/px | 32.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,389.9 | 5.119/px | 28.4 | 1.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 5,651.4 | 2.165/px | 28.4 | 1.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 6,360.8 | 1.923/px | 28.4 | 1.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 5,263.7 | 2.324/px | 28.4 | 1.8 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 230.9 | 52.988/px | 32.2 | 3.3 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 2,777.6 | 4.404/px | 32.2 | 3.3 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,864.2 | 4.271/px | 32.2 | 3.3 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 2,049.5 | 5.969/px | 32.2 | 3.3 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | HW? | 34.5 | 354.154/px | 32.2 | 1,108.9 | D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | HW | 36.8 | 332.733/px | 32.2 | 1,108.9 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 74.1 | 165.050/px | 32.2 | 1,108.9 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | HW | 36.5 | 334.896/px | 32.2 | 1,108.9 | D3; D4; not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,528.8 | 4.837/px | - | 0.7 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 9,769.7 | 1.252/px | - | 0.7 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 10,401.2 | 1.176/px | - | 0.7 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 9,340.7 | 1.310/px | - | 0.7 |  |
| Dd.ColorFill.Vid.16x16 | HW | 238.7 | 51.248/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 3,781.3 | 3.235/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 3,868.9 | 3.162/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 2,802.0 | 4.366/px | 32.2 | 2.7 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,399.008/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,410.675/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,397.672/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 960.585/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 49,764,768.000/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 15.8 | 774.750/px | 32.2 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 16.1 | 760.182/px | 32.2 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 16.1 | 761.717/px | 32.2 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 16.0 | 763.997/px | 32.2 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 16.0 | 765.756/px | 32.2 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 15.8 | 772.251/px | 32.2 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 32.5 | 375.981/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 32.2 | 379.863/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 31.2 | 391.998/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 32.4 | 377.852/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 32.1 | 381.597/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 32.4 | 377.089/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 32.4 | 377.972/px | 32.2 | 5.5 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 29.6 | 413.162/px | 32.2 | 5.5 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 32.6 | 375.480/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 32.2 | 380.135/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 31.0 | 395.103/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 32.3 | 378.172/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 32.0 | 382.228/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 32.4 | 377.229/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 32.3 | 378.228/px | 32.2 | 6.3 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 28.8 | 425.466/px | 32.2 | 6.3 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 28.3 | 432.490/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 28.1 | 434.910/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 28.2 | 433.306/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 28.3 | 432.423/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 28.2 | 433.164/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 28.3 | 431.796/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 28.3 | 432.663/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 26.4 | 463.992/px | 28.4 | 5.1 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 15.5 | 787.681/px | 32.2 | 1.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 15.6 | 783.730/px | 32.2 | 1.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 15.6 | 786.647/px | 32.2 | 1.2 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 15.5 | 788.985/px | 32.2 | 1.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 15.5 | 786.819/px | 32.2 | 1.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 15.0 | 814.127/px | 32.2 | 1.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 15.4 | 796.614/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 15.3 | 799.106/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 15.4 | 794.591/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 15.3 | 801.293/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 15.4 | 794.937/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 15.3 | 800.256/px | 32.2 | 2.3 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 15.5 | 786.911/px | 32.2 | 2.3 |  |
| Gdi.Ellipse.128x128 | CPU | 30.7 | 399.030/px | 32.2 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 1,829,793.440/string | 32.2 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 356,639.674/string | 32.2 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 1,435,843.063/string | 32.2 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 211,183.916/string | 32.2 | - |  |
| Gdi.FillRect.1024x768 | CPU | 32.2 | 379.380/px | 32.2 | 4.9 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 32.0 | 382.163/px | 32.2 | 4.9 |  |
| Gdi.FillRect.16x16 | CPU | 31.2 | 391.509/px | 32.2 | 4.9 |  |
| Gdi.FillRect.256x256 | CPU | 32.2 | 380.289/px | 32.2 | 4.9 |  |
| Gdi.FillRect.32x32 | CPU | 32.0 | 381.813/px | 32.2 | 4.9 |  |
| Gdi.FillRect.512x512 | CPU | 32.2 | 379.557/px | 32.2 | 4.9 |  |
| Gdi.FillRect.64x64 | CPU | 32.2 | 379.485/px | 32.2 | 4.9 |  |
| Gdi.FillRect.8x8 | CPU | 28.5 | 429.837/px | 32.2 | 4.9 |  |
| Gdi.LineDiagonal30 | n/a | - | 34,788.152/line | 32.2 | - |  |
| Gdi.LineHorizontal | n/a | - | 40,445.908/line | 32.2 | - |  |
| Gdi.LineVertical | n/a | - | 77,073.520/line | 32.2 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 32.2 | 379.649/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.128x128 | CPU | 32.0 | 382.148/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.16x16 | CPU | 30.4 | 402.847/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.256x256 | CPU | 32.1 | 381.219/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.32x32 | CPU | 31.7 | 385.461/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.512x512 | CPU | 32.2 | 379.866/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.64x64 | CPU | 32.1 | 381.290/px | 32.2 | 7.2 |  |
| Gdi.PatBltHatch.8x8 | CPU | 26.7 | 458.959/px | 32.2 | 7.2 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 32.2 | 379.809/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.128x128 | CPU | 32.0 | 382.640/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.16x16 | CPU | 30.3 | 403.163/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.256x256 | CPU | 32.1 | 380.866/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.32x32 | CPU | 31.7 | 385.343/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.512x512 | CPU | 32.2 | 380.174/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.64x64 | CPU | 32.1 | 381.298/px | 32.2 | 5.5 |  |
| Gdi.PatBltPattern.8x8 | CPU | 26.7 | 458.233/px | 32.2 | 5.5 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 32.2 | 379.697/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 32.0 | 382.049/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 31.9 | 383.988/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.256x256 | CPU | 32.1 | 380.699/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 32.0 | 381.735/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.512x512 | CPU | 32.2 | 379.406/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.64x64 | CPU | 32.3 | 378.938/px | 32.2 | 6.2 |  |
| Gdi.PatBltSolid.8x8 | CPU | 30.7 | 398.299/px | 32.2 | 6.2 |  |
| Gdi.Polygon.128x128 | CPU | 29.5 | 415.215/px | 32.2 | - |  |
| Gdi.Polyline | n/a | - | 38,418.580/line | 32.2 | - |  |
| Gdi.Rectangle.128x128 | CPU | 31.8 | 384.459/px | 32.2 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 32.3 | 378.550/px | 32.2 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 15.5 | 790.728/px | 32.2 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 32.3 | 378.313/px | 32.2 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 10.0 | 1,228.358/px | 32.2 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 10.0 | 1,221.977/px | 32.2 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 15.5 | 786.720/px | 32.2 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 10.0 | 1,222.614/px | 32.2 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 32.3 | 378.439/px | 32.2 | - |  |
| Gdi.RoundRect.128x128 | CPU | 31.2 | 392.287/px | 32.2 | - |  |
| Gdi.ScrollBitBlt | CPU | 15.3 | 800.302/px | 32.2 | - |  |
| Gdi.ScrollDC | CPU | 15.3 | 799.869/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 32.4 | 377.235/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 32.4 | 377.530/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 32.4 | 377.228/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 32.4 | 377.789/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 32.3 | 379.275/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 32.3 | 379.168/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 32.3 | 379.252/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 32.2 | 379.317/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 32.2 | 380.025/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 32.1 | 381.021/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 32.2 | 380.175/px | 32.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 32.1 | 380.502/px | 32.2 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 5.6 | 2,202.804/px | 32.2 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 5.5 | 2,229.487/px | 32.2 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 5.6 | 2,200.601/px | 32.2 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 5.5 | 2,224.470/px | 32.2 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 23.6 | 519.092/px | 32.2 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 23.8 | 513.110/px | 32.2 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 23.6 | 519.123/px | 32.2 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 23.9 | 512.142/px | 32.2 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 31.9 | 383.507/px | 32.2 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 32.1 | 381.360/px | 32.2 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 29.3 | 417.618/px | 32.2 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 30.1 | 406.290/px | 32.2 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 1,831,139.920/string | 32.2 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 356,560.094/string | 32.2 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 1,437,484.254/string | 32.2 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 210,579.614/string | 32.2 | - |  |

## CLAIMED-NOT-DELIVERED

None.
