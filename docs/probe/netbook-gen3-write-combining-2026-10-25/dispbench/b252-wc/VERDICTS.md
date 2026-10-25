# Verdicts: w9qi2be2-gma950-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x576x16, build 31b28a4, driver Velocity9x Intel GMA 950 (945GSE)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 2,814.4 |
| FbRead | 25.7 |
| MemFill | 1,267.0 |
| MemCopy | 893.9 |
| DibToDibBlt | 876.9 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 57,685.7 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 34 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 138.6 | 23.983/px | - | 125.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 254.4 | 13.069/px | - | 125.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,161.6 | 2.862/px | - | 125.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 235.8 | 14.100/px | - | 125.6 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 335.3 | 9.916/px | - | -2.9 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,356.1 | 2.452/px | - | -2.9 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,213.1 | 2.741/px | - | -2.9 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 1,264.9 | 2.629/px | - | -2.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 313.5 | 10.604/px | - | -4.5 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 2,295.6 | 1.448/px | - | -4.5 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,729.0 | 1.923/px | - | -4.5 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,505.4 | 2.209/px | - | -4.5 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 58.8 | 56.568/px | 2,814.4 | 28.8 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 254.8 | 13.047/px | 2,814.4 | 28.8 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 298.1 | 11.155/px | 2,814.4 | 28.8 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 203.8 | 16.314/px | 2,814.4 | 28.8 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 69.0 | 48.154/px | 2,814.4 | 4.2 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | CPU-fast | 1,756.3 | 1.893/px | 2,814.4 | 4.2 | not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | CPU-fast | 1,661.8 | 2.001/px | 2,814.4 | 4.2 | not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU-fast | 714.6 | 4.653/px | 2,814.4 | 4.2 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 68.4 | 48.615/px | 2,814.4 | 7.5 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | CPU-fast | 2,373.1 | 1.401/px | 2,814.4 | 7.5 | not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | CPU-fast | 2,760.4 | 1.205/px | 2,814.4 | 7.5 | not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU-fast | 900.3 | 3.693/px | 2,814.4 | 7.5 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 18.6 | 178.868/px | 25.7 | -11.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 23.9 | 139.218/px | 25.7 | -11.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 23.6 | 140.629/px | 25.7 | -11.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 23.4 | 142.217/px | 25.7 | -11.8 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 32.8 | 101.411/px | 25.7 | 6.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 51.0 | 65.218/px | 25.7 | 6.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 51.1 | 65.095/px | 25.7 | 6.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 49.2 | 67.570/px | 25.7 | 6.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 46.4 | 71.729/px | 25.7 | 4.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 97.1 | 34.231/px | 25.7 | 4.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 97.0 | 34.271/px | 25.7 | 4.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 89.4 | 37.206/px | 25.7 | 4.7 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 15.9 | 209.104/px | 2,814.4 | 18.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 23.8 | 139.935/px | 2,814.4 | 18.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 23.9 | 139.101/px | 2,814.4 | 18.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 23.0 | 144.492/px | 2,814.4 | 18.8 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 72.7 | 45.744/px | 2,814.4 | 5.2 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU-fast | 1,077.2 | 3.087/px | 2,814.4 | 5.2 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU-fast | 1,117.6 | 2.975/px | 2,814.4 | 5.2 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU-fast | 775.9 | 4.286/px | 2,814.4 | 5.2 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 27.7 | 120.084/px | 2,814.4 | 13.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 100.0 | 33.239/px | 2,814.4 | 13.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 101.1 | 32.874/px | 2,814.4 | 13.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 88.9 | 37.396/px | 2,814.4 | 13.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 375.9 | 8.846/px | - | -3.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,366.3 | 2.434/px | - | -3.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,215.8 | 2.735/px | - | -3.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 1,303.0 | 2.552/px | - | -3.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 55.4 | 60.030/px | 2,814.4 | 3.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU-fast | 1,841.0 | 1.806/px | 2,814.4 | 3.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | CPU-fast | 1,664.2 | 1.998/px | 2,814.4 | 3.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU-fast | 794.5 | 4.185/px | 2,814.4 | 3.6 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 321.4 | 10.345/px | 25.7 | -3.2 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,367.0 | 2.432/px | 25.7 | -3.2 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,199.9 | 2.771/px | 25.7 | -3.2 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 1,171.3 | 2.839/px | 25.7 | -3.2 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 76.3 | 43.557/px | 2,814.4 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU-fast | 1,075.8 | 3.091/px | 2,814.4 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU-fast | 1,117.0 | 2.977/px | 2,814.4 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU-fast | 776.8 | 4.280/px | 2,814.4 | 5.0 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 60.3 | 55.176/px | 2,814.4 | 29.6 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 249.4 | 13.332/px | 2,814.4 | 29.6 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 292.9 | 11.351/px | 2,814.4 | 29.6 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 202.7 | 16.401/px | 2,814.4 | 29.6 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 400.0 | 8.313/px | - | 1.5 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,387.4 | 1.393/px | - | 1.5 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,452.0 | 1.356/px | - | 1.5 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,628.1 | 2.042/px | - | 1.5 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 78.2 | 42.503/px | 2,814.4 | 4.4 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU-fast | 2,071.8 | 1.605/px | 2,814.4 | 4.4 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU-fast | 2,152.7 | 1.545/px | 2,814.4 | 4.4 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU-fast | 1,182.1 | 2.813/px | 2,814.4 | 4.4 | claimed |
| Dd.Lock.Back | n/a | - | 8,345.423/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 8,358.372/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 8,295.825/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,216.229/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 551,757.727/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 24.9 | 133.424/px | 2,814.4 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 21.5 | 154.621/px | 2,814.4 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 24.6 | 135.277/px | 2,814.4 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 21.6 | 154.288/px | 2,814.4 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 24.7 | 134.552/px | 2,814.4 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 24.9 | 133.629/px | 2,814.4 | - |  |
| Gdi.BitBltBitmapToScreen.1024x576 | CPU | 343.8 | 9.671/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 317.0 | 10.488/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 61.5 | 54.030/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 338.8 | 9.814/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 169.1 | 19.667/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 342.2 | 9.716/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 272.9 | 12.183/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 19.1 | 174.296/px | 2,814.4 | 6.5 |  |
| Gdi.BitBltDibToScreen.1024x576 | CPU | 419.9 | 7.918/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 369.5 | 8.998/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 38.7 | 85.870/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 412.6 | 8.059/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 107.4 | 30.972/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 417.6 | 7.961/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 179.7 | 18.498/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 11.5 | 288.285/px | 2,814.4 | 14.0 |  |
| Gdi.BitBltScreenToBitmap.1024x576 | CPU | 24.0 | 138.793/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 23.5 | 141.286/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 18.7 | 178.119/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 24.0 | 138.741/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 23.6 | 140.984/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 24.0 | 138.367/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 24.9 | 133.721/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 11.4 | 292.826/px | 25.7 | 7.3 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 25.3 | 131.569/px | 2,814.4 | 8.0 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 19.1 | 173.821/px | 2,814.4 | 8.0 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 25.5 | 130.429/px | 2,814.4 | 8.0 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 23.4 | 141.987/px | 2,814.4 | 8.0 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.9 | 133.445/px | 2,814.4 | 8.0 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 11.3 | 293.837/px | 2,814.4 | 8.0 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 25.1 | 132.549/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 17.8 | 186.370/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 25.3 | 131.266/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 22.9 | 145.041/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 25.5 | 130.442/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.0 | 138.392/px | 2,814.4 | 15.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 11.4 | 292.096/px | 2,814.4 | 15.8 |  |
| Gdi.Ellipse.128x128 | CPU | 93.1 | 35.717/px | 2,814.4 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 82,012.073/string | 2,814.4 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 70,706.792/string | 2,814.4 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 76,585.927/string | 2,814.4 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 68,144.974/string | 2,814.4 | - |  |
| Gdi.FillRect.1024x576 | CPU-fast | 1,884.6 | 1.764/px | 2,814.4 | 8.8 | D2 0.06 |
| Gdi.FillRect.128x128 | CPU-fast | 1,204.6 | 2.760/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.16x16 | CPU | 55.7 | 59.742/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.256x256 | CPU-fast | 1,627.7 | 2.043/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.32x32 | CPU | 229.4 | 14.496/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.512x512 | CPU-fast | 1,820.9 | 1.826/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.64x64 | CPU-fast | 641.5 | 5.183/px | 2,814.4 | 8.8 |  |
| Gdi.FillRect.8x8 | CPU | 16.4 | 203.047/px | 2,814.4 | 8.8 |  |
| Gdi.LineDiagonal30 | n/a | - | 14,227.203/line | 2,814.4 | - |  |
| Gdi.LineHorizontal | n/a | - | 9,336.492/line | 2,814.4 | - |  |
| Gdi.LineVertical | n/a | - | 19,051.696/line | 2,814.4 | - |  |
| Gdi.PatBltHatch.1024x576 | CPU | 487.9 | 6.815/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.128x128 | CPU | 433.4 | 7.672/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.16x16 | CPU | 61.4 | 54.157/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.256x256 | CPU | 471.4 | 7.053/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.32x32 | CPU | 194.3 | 17.109/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.512x512 | CPU | 484.4 | 6.864/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.64x64 | CPU | 350.8 | 9.477/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltHatch.8x8 | CPU | 18.7 | 177.528/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.1024x576 | CPU | 488.1 | 6.812/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.128x128 | CPU | 433.4 | 7.672/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.16x16 | CPU | 61.5 | 54.073/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.256x256 | CPU | 471.2 | 7.056/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.32x32 | CPU | 194.3 | 17.117/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.512x512 | CPU | 484.1 | 6.868/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.64x64 | CPU | 350.7 | 9.480/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltPattern.8x8 | CPU | 18.7 | 178.137/px | 2,814.4 | 7.3 |  |
| Gdi.PatBltSolid.1024x576 | CPU-fast | 1,892.1 | 1.757/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 1,372.4 | 2.423/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.16x16 | CPU | 85.3 | 38.957/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 1,699.1 | 1.957/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.32x32 | CPU | 359.9 | 9.238/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 1,840.5 | 1.807/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.64x64 | CPU-fast | 862.7 | 3.854/px | 2,814.4 | 5.5 |  |
| Gdi.PatBltSolid.8x8 | CPU | 27.6 | 120.306/px | 2,814.4 | 5.5 |  |
| Gdi.Polygon.128x128 | CPU | 153.7 | 21.636/px | 2,814.4 | - |  |
| Gdi.Polyline | n/a | - | 1,439.586/line | 2,814.4 | - |  |
| Gdi.Rectangle.128x128 | CPU | 509.6 | 6.525/px | 2,814.4 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 355.5 | 9.352/px | 2,814.4 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.7 | 134.532/px | 2,814.4 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 351.8 | 9.450/px | 2,814.4 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 8.9 | 375.511/px | 2,814.4 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 8.9 | 374.669/px | 2,814.4 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.9 | 133.603/px | 2,814.4 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 8.9 | 374.158/px | 2,814.4 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 356.2 | 9.336/px | 2,814.4 | - |  |
| Gdi.RoundRect.128x128 | CPU | 212.8 | 15.622/px | 2,814.4 | - |  |
| Gdi.ScrollBitBlt | CPU | 25.5 | 130.304/px | 2,814.4 | - |  |
| Gdi.ScrollDC | CPU | 25.5 | 130.583/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x576 | CPU | 401.6 | 8.279/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x288 | CPU | 410.0 | 8.109/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 | CPU | 411.1 | 8.088/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x288 | CPU | 409.1 | 8.127/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x576 | CPU | 124.1 | 26.790/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x288 | CPU | 132.0 | 25.197/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x576 | CPU | 134.2 | 24.771/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x288 | CPU | 133.3 | 24.937/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x576 | CPU | 153.6 | 21.653/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x288 | CPU | 156.0 | 21.319/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x576 | CPU | 157.0 | 21.177/px | 2,814.4 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x288 | CPU | 156.0 | 21.310/px | 2,814.4 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.4 | 970.709/px | 2,814.4 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.3 | 1,020.398/px | 2,814.4 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.2 | 1,047.219/px | 2,814.4 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 3.2 | 1,032.116/px | 2,814.4 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 21.7 | 152.973/px | 2,814.4 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 24.0 | 138.577/px | 2,814.4 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 21.8 | 152.806/px | 2,814.4 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 23.9 | 138.902/px | 2,814.4 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 25.4 | 130.881/px | 2,814.4 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 30.8 | 107.996/px | 2,814.4 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 20.8 | 160.021/px | 2,814.4 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 24.5 | 135.986/px | 2,814.4 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 81,687.660/string | 2,814.4 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 70,681.633/string | 2,814.4 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 76,637.233/string | 2,814.4 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 68,284.229/string | 2,814.4 | - |  |

## CLAIMED-NOT-DELIVERED

- Dd.Blt.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.256x256: the caps claim it, the timing says CPU-fast
- Dd.Blt.VidToVid.Plain.512x512: the caps claim it, the timing says CPU-fast
- Dd.Blt.VidToVid.Plain.64x64: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.256x256: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.512x512: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.64x64: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.16x16: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU-fast
