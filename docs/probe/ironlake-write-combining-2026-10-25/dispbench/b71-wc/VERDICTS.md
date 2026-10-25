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
| FbWrite | 3,992.1 |
| FbRead | 107.6 |
| MemFill | 21,682.4 |
| MemCopy | 8,986.1 |
| DibToDibBlt | 8,510.4 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 36 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 1,440.7 | 8.491/px | - | 10.8 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 3,231.5 | 3.785/px | - | 10.8 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 5,474.0 | 2.235/px | - | 10.8 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 2,704.2 | 4.524/px | - | 10.8 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,022.5 | 6.048/px | - | 1.8 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 5,589.6 | 2.188/px | - | 1.8 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU | 6,282.9 | 1.947/px | - | 1.8 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 5,328.3 | 2.296/px | - | 1.8 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,903.5 | 6.426/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 8,380.0 | 1.460/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 9,136.4 | 1.339/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 7,072.9 | 1.730/px | - | 1.1 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 444.5 | 27.522/px | 3,992.1 | 23.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 976.1 | 12.533/px | 3,992.1 | 23.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 1,344.8 | 9.097/px | 3,992.1 | 23.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 914.6 | 13.375/px | 3,992.1 | 23.6 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 601.0 | 20.352/px | 3,992.1 | 2.6 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | CPU | 2,769.6 | 4.417/px | 3,992.1 | 2.6 | not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | CPU | 2,946.2 | 4.152/px | 3,992.1 | 2.6 | not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 2,473.1 | 4.946/px | 3,992.1 | 2.6 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 663.3 | 18.443/px | 3,992.1 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | CPU | 3,245.0 | 3.770/px | 3,992.1 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | CPU | 3,526.0 | 3.469/px | 3,992.1 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 2,755.0 | 4.440/px | 3,992.1 | 2.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 27.0 | 453.288/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 27.9 | 438.491/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 27.9 | 438.377/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 27.8 | 440.733/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 53.5 | 228.683/px | 107.6 | 6.9 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 55.8 | 219.164/px | 107.6 | 6.9 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 56.1 | 218.179/px | 107.6 | 6.9 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 55.8 | 219.352/px | 107.6 | 6.9 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 99.5 | 122.950/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 111.7 | 109.551/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 111.9 | 109.316/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 110.9 | 110.321/px | 107.6 | 2.4 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 15.6 | 782.451/px | 3,992.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 15.7 | 777.732/px | 3,992.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 15.7 | 778.378/px | 3,992.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 15.7 | 779.479/px | 3,992.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 417.1 | 29.326/px | 3,992.1 | 2.5 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 2,778.3 | 4.403/px | 3,992.1 | 2.5 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU | 2,864.3 | 4.271/px | 3,992.1 | 2.5 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 2,051.2 | 5.964/px | 3,992.1 | 2.5 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 67.6 | 181.085/px | 3,992.1 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 74.5 | 164.230/px | 3,992.1 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 74.7 | 163.695/px | 3,992.1 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 74.2 | 164.942/px | 3,992.1 | 4.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,308.2 | 5.300/px | - | 1.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 5,601.0 | 2.184/px | - | 1.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU | 6,288.0 | 1.945/px | - | 1.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 5,528.7 | 2.213/px | - | 1.7 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 619.9 | 19.734/px | 3,992.1 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU | 2,771.8 | 4.413/px | 3,992.1 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | CPU | 2,945.8 | 4.153/px | 3,992.1 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 2,495.6 | 4.902/px | 3,992.1 | 2.5 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,381.3 | 5.137/px | 107.6 | 1.6 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 5,674.7 | 2.156/px | 107.6 | 1.6 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 6,379.1 | 1.918/px | 107.6 | 1.6 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 5,864.5 | 2.086/px | 107.6 | 1.6 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 426.9 | 28.652/px | 3,992.1 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 2,778.2 | 4.403/px | 3,992.1 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU | 2,864.4 | 4.271/px | 3,992.1 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 2,051.3 | 5.963/px | 3,992.1 | 2.5 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 476.4 | 25.675/px | 3,992.1 | 22.0 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 981.1 | 12.469/px | 3,992.1 | 22.0 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 1,325.9 | 9.226/px | 3,992.1 | 22.0 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 962.6 | 12.708/px | 3,992.1 | 22.0 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,527.3 | 4.840/px | - | 0.7 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 9,768.4 | 1.252/px | - | 0.7 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 10,400.9 | 1.176/px | - | 0.7 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 8,652.5 | 1.414/px | - | 0.7 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 409.0 | 29.906/px | 3,992.1 | 2.0 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 3,781.5 | 3.235/px | 3,992.1 | 2.0 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 3,869.3 | 3.161/px | 3,992.1 | 2.0 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 2,801.5 | 4.366/px | 3,992.1 | 2.0 | claimed |
| Dd.Lock.Back | n/a | - | 3,399.838/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,407.317/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,398.763/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 959.955/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 1,093,049.687/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 15.2 | 803.548/px | 3,992.1 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 15.5 | 789.337/px | 3,992.1 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 15.5 | 791.141/px | 3,992.1 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 15.4 | 793.064/px | 3,992.1 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 15.4 | 794.933/px | 3,992.1 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 15.3 | 801.003/px | 3,992.1 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 2,904.3 | 4.212/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 1,967.1 | 6.219/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 942.3 | 12.981/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 2,402.5 | 5.092/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 2,192.2 | 5.580/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 2,684.8 | 4.556/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 3,210.0 | 3.811/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 236.9 | 51.643/px | 3,992.1 | 3.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,184.8 | 3.841/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 1,955.5 | 6.256/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 755.8 | 16.186/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,491.9 | 4.909/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 1,909.4 | 6.407/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,847.1 | 4.297/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 3,120.3 | 3.920/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 190.7 | 64.160/px | 3,992.1 | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 106.0 | 115.410/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 79.7 | 153.419/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 28.2 | 433.793/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 92.8 | 131.810/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 28.2 | 433.071/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 101.4 | 120.677/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 28.3 | 432.509/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 26.3 | 464.497/px | 107.6 | 163.6 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 43.0 | 284.535/px | 3,992.1 | 253.9 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 15.0 | 815.102/px | 3,992.1 | 253.9 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 49.9 | 244.939/px | 3,992.1 | 253.9 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 14.9 | 819.640/px | 3,992.1 | 253.9 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 15.0 | 816.593/px | 3,992.1 | 253.9 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 14.4 | 848.769/px | 3,992.1 | 253.9 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 14.8 | 827.886/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 14.7 | 830.078/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 14.8 | 826.686/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 14.7 | 833.305/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 14.8 | 826.427/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 14.7 | 830.549/px | 3,992.1 | 3.5 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 15.0 | 817.835/px | 3,992.1 | 3.5 |  |
| Gdi.Ellipse.128x128 | CPU | 491.5 | 24.886/px | 3,992.1 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 35,089.338/string | 3,992.1 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 59,383.023/string | 3,992.1 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 34,394.248/string | 3,992.1 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 49,098.373/string | 3,992.1 | - |  |
| Gdi.FillRect.1024x768 | CPU | 3,986.9 | 3.068/px | 3,992.1 | 5.8 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 1,970.9 | 6.207/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.16x16 | CPU | 661.6 | 18.489/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.256x256 | CPU | 2,650.5 | 4.615/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.32x32 | CPU | 1,779.4 | 6.875/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.512x512 | CPU | 3,336.2 | 3.667/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.64x64 | CPU | 3,016.8 | 4.055/px | 3,992.1 | 5.8 |  |
| Gdi.FillRect.8x8 | CPU | 166.8 | 73.327/px | 3,992.1 | 5.8 |  |
| Gdi.LineDiagonal30 | n/a | - | 16,544.793/line | 3,992.1 | - |  |
| Gdi.LineHorizontal | n/a | - | 3,867.802/line | 3,992.1 | - |  |
| Gdi.LineVertical | n/a | - | 68,170.948/line | 3,992.1 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 3,840.8 | 3.185/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,179.2 | 3.848/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.16x16 | CPU | 380.6 | 32.139/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.256x256 | CPU | 3,323.8 | 3.680/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,230.7 | 9.940/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.512x512 | CPU | 3,566.9 | 3.430/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,464.4 | 4.964/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltHatch.8x8 | CPU | 95.9 | 127.567/px | 3,992.1 | 2.8 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 3,833.8 | 3.191/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,179.6 | 3.847/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.16x16 | CPU | 490.9 | 24.919/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.256x256 | CPU | 3,325.9 | 3.678/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,498.6 | 8.163/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.512x512 | CPU | 3,571.8 | 3.425/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,714.3 | 4.507/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltPattern.8x8 | CPU | 124.5 | 98.231/px | 3,992.1 | 2.4 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 3,997.2 | 3.060/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.128x128 | CPU | 2,009.3 | 6.088/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.16x16 | CPU | 1,138.8 | 10.742/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.256x256 | CPU | 2,667.2 | 4.586/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,485.5 | 4.922/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.512x512 | CPU | 3,345.9 | 3.656/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.64x64 | CPU | 3,419.9 | 3.577/px | 3,992.1 | 5.1 |  |
| Gdi.PatBltSolid.8x8 | CPU | 286.6 | 42.685/px | 3,992.1 | 5.1 |  |
| Gdi.Polygon.128x128 | CPU | 430.9 | 28.387/px | 3,992.1 | - |  |
| Gdi.Polyline | n/a | - | 1,240.107/line | 3,992.1 | - |  |
| Gdi.Rectangle.128x128 | CPU | 838.0 | 14.597/px | 3,992.1 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 1,069.9 | 11.434/px | 3,992.1 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 14.9 | 820.662/px | 3,992.1 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 1,069.7 | 11.436/px | 3,992.1 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 9.7 | 1,260.953/px | 3,992.1 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 9.7 | 1,258.990/px | 3,992.1 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 15.0 | 816.936/px | 3,992.1 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 9.7 | 1,258.386/px | 3,992.1 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 1,070.3 | 11.430/px | 3,992.1 | - |  |
| Gdi.RoundRect.128x128 | CPU | 775.8 | 15.767/px | 3,992.1 | - |  |
| Gdi.ScrollBitBlt | CPU | 55.0 | 222.552/px | 3,992.1 | - |  |
| Gdi.ScrollDC | CPU | 54.9 | 222.677/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 447.0 | 27.365/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 446.2 | 27.416/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 447.3 | 27.345/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 446.6 | 27.393/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 1,290.7 | 9.478/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 1,290.2 | 9.481/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 1,290.6 | 9.479/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 1,291.5 | 9.472/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 3,186.3 | 3.839/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 3,064.2 | 3.992/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 3,177.5 | 3.850/px | 3,992.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 3,069.6 | 3.985/px | 3,992.1 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 17.8 | 687.441/px | 3,992.1 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 15.4 | 794.292/px | 3,992.1 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 17.8 | 687.120/px | 3,992.1 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 15.4 | 794.030/px | 3,992.1 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 83.6 | 146.371/px | 3,992.1 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 166.5 | 73.475/px | 3,992.1 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 83.6 | 146.276/px | 3,992.1 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 166.5 | 73.471/px | 3,992.1 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 2,650.5 | 4.615/px | 3,992.1 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 2,767.0 | 4.421/px | 3,992.1 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 279.4 | 43.783/px | 3,992.1 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 372.0 | 32.886/px | 3,992.1 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 37,137.618/string | 3,992.1 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 59,072.169/string | 3,992.1 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 34,336.245/string | 3,992.1 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 48,690.915/string | 3,992.1 | - |  |

## CLAIMED-NOT-DELIVERED

- Dd.Blt.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.512x512: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.512x512: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.16x16: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU
