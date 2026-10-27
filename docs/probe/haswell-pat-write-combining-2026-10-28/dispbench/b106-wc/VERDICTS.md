# Verdicts: w9qi5a83-hd4600-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 800x600x16, build f2dd1cd, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 9,238.1 |
| FbRead | 216.7 |
| MemFill | 31,056.8 |
| MemCopy | 20,032.7 |
| DibToDibBlt | 8,684.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 230 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 3404 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 874.5 | 7.301/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,796.5 | 3.554/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,448.3 | 0.611/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,417.5 | 4.505/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,221.0 | 2.875/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,957.7 | 0.534/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,676.2 | 0.435/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,257.1 | 0.623/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,930.5 | 3.308/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 14,046.8 | 0.455/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,696.4 | 0.382/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 9,017.4 | 0.708/px | - | 0.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 315.8 | 20.217/px | 9,238.1 | 17.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 1,724.8 | 3.702/px | 9,238.1 | 17.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 5,876.1 | 1.087/px | 9,238.1 | 17.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 1,187.8 | 5.376/px | 9,238.1 | 17.9 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 383.2 | 16.664/px | 9,238.1 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 12,555.8 | 0.509/px | 9,238.1 | 1.6 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 16,019.7 | 0.399/px | 9,238.1 | 1.6 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 4,836.0 | 1.320/px | 9,238.1 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 376.2 | 16.971/px | 9,238.1 | 1.3 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 15,920.8 | 0.401/px | 9,238.1 | 1.3 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 17,899.9 | 0.357/px | 9,238.1 | 1.3 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 4,496.9 | 1.420/px | 9,238.1 | 1.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 32.7 | 195.286/px | 216.7 | 1.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.191/px | 216.7 | 1.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.5 | 180.041/px | 216.7 | 1.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.866/px | 216.7 | 1.8 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 61.4 | 104.028/px | 216.7 | 0.7 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 70.6 | 90.452/px | 216.7 | 0.7 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.6 | 90.434/px | 216.7 | 0.7 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 70.4 | 90.727/px | 216.7 | 0.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 108.2 | 59.028/px | 216.7 | 2.0 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 140.8 | 45.349/px | 216.7 | 2.0 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 141.4 | 45.157/px | 216.7 | 2.0 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 138.9 | 45.962/px | 216.7 | 2.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 23.0 | 278.095/px | 9,238.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 25.0 | 255.903/px | 9,238.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.9 | 256.578/px | 9,238.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.9 | 256.567/px | 9,238.1 | -3.2 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 197.6 | 32.318/px | 9,238.1 | 3.2 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 2,453.5 | 2.603/px | 9,238.1 | 3.2 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,621.0 | 2.436/px | 9,238.1 | 3.2 | D1 3404; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 1,284.1 | 4.972/px | 9,238.1 | 3.2 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 78.0 | 81.889/px | 9,238.1 | 5.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 112.9 | 56.541/px | 9,238.1 | 5.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 114.3 | 55.856/px | 9,238.1 | 5.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 111.8 | 57.103/px | 9,238.1 | 5.4 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,513.9 | 2.540/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,310.6 | 0.565/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,630.5 | 0.436/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,669.7 | 0.598/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 391.6 | 16.305/px | 9,238.1 | 1.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 12,735.8 | 0.501/px | 9,238.1 | 1.6 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 16,208.8 | 0.394/px | 9,238.1 | 1.6 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 4,929.4 | 1.295/px | 9,238.1 | 1.6 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | CPU | 61.5 | 103.856/px | 216.7 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | CPU | 70.6 | 90.428/px | 216.7 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | CPU | 70.7 | 90.343/px | 216.7 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | CPU | 70.4 | 90.687/px | 216.7 | 1.1 | not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 201.2 | 31.728/px | 9,238.1 | 3.2 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 2,450.3 | 2.606/px | 9,238.1 | 3.2 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,620.8 | 2.436/px | 9,238.1 | 3.2 | D1 3404; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 1,284.7 | 4.970/px | 9,238.1 | 3.2 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 23.0 | 277.969/px | 9,238.1 | -0.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 24.9 | 256.072/px | 9,238.1 | -0.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 24.9 | 256.372/px | 9,238.1 | -0.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 24.9 | 256.887/px | 9,238.1 | -0.8 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,771.7 | 2.304/px | - | 0.5 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 15,502.5 | 0.412/px | - | 0.5 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 18,935.0 | 0.337/px | - | 0.5 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,369.7 | 0.415/px | - | 0.5 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 254.7 | 25.074/px | 9,238.1 | 2.1 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 4,715.6 | 1.354/px | 9,238.1 | 2.1 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 4,974.0 | 1.284/px | 9,238.1 | 2.1 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 1,998.2 | 3.196/px | 9,238.1 | 2.1 | claimed |
| Dd.Lock.Back | n/a | - | 3,393.605/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,399.131/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,391.482/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 558.476/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 643,833.693/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 24.8 | 257.338/px | 9,238.1 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 23.6 | 270.810/px | 9,238.1 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.8 | 257.414/px | 9,238.1 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 23.6 | 270.480/px | 9,238.1 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.8 | 257.616/px | 9,238.1 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.8 | 257.939/px | 9,238.1 | - | D6 |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 8,357.0 | 0.764/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 478.8 | 13.336/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,407.4 | 1.449/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,873.1 | 3.409/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 7,175.4 | 0.890/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 4,753.0 | 1.343/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.800x600 | CPU-fast | 9,138.0 | 0.699/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 131.6 | 48.537/px | 9,238.1 | 2.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 3,507.6 | 1.820/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 243.9 | 26.185/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 4,170.5 | 1.531/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 872.1 | 7.322/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 4,391.3 | 1.454/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 2,191.3 | 2.914/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.800x600 | CPU | 4,362.1 | 1.464/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 67.0 | 95.324/px | 9,238.1 | 1.9 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.488/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.3 | 191.723/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 139.6 | 45.754/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.9 | 183.128/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 178.9 | 35.699/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 180.923/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.800x600 | CPU | 214.2 | 29.804/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.4 | 224.975/px | 216.7 | 201.2 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.7 | 258.307/px | 9,238.1 | 233.5 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.6 | 270.444/px | 9,238.1 | 233.5 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 104.0 | 61.404/px | 9,238.1 | 233.5 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.5 | 261.136/px | 9,238.1 | 233.5 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.7 | 258.297/px | 9,238.1 | 233.5 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.1 | 303.016/px | 9,238.1 | 233.5 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.1 | 264.531/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.5 | 283.385/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.1 | 264.779/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.7 | 269.432/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.1 | 264.420/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.0 | 266.026/px | 9,238.1 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.7 | 308.755/px | 9,238.1 | 2.2 |  |
| Gdi.Ellipse.128x128 | CPU | 804.3 | 7.939/px | 9,238.1 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 20,855.640/string | 9,238.1 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 15,607.491/string | 9,238.1 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 23,729.588/string | 9,238.1 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 19,563.939/string | 9,238.1 | - |  |
| Gdi.FillRect.128x128 | CPU-fast | 6,805.5 | 0.938/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.16x16 | CPU | 242.2 | 26.365/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.256x256 | CPU | 4,346.4 | 1.469/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.32x32 | CPU | 974.5 | 6.552/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.512x512 | CPU-fast | 7,154.1 | 0.893/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.64x64 | CPU | 3,010.4 | 2.121/px | 9,238.1 | 2.9 |  |
| Gdi.FillRect.800x600 | CPU-fast | 9,286.1 | 0.688/px | 9,238.1 | 2.9 | D2 0.00 |
| Gdi.FillRect.8x8 | CPU | 63.8 | 100.025/px | 9,238.1 | 2.9 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,746.064/line | 9,238.1 | - |  |
| Gdi.LineHorizontal | n/a | - | 3,371.277/line | 9,238.1 | - |  |
| Gdi.LineVertical | n/a | - | 6,909.097/line | 9,238.1 | - |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 6,674.8 | 0.957/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.16x16 | CPU | 394.7 | 16.176/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 9,250.9 | 0.690/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,067.5 | 5.982/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 9,711.8 | 0.657/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.64x64 | CPU | 3,190.9 | 2.001/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltHatch.800x600 | HW | 10,167.7 | 0.628/px | 9,238.1 | 1.4 | D3 |
| Gdi.PatBltHatch.8x8 | CPU | 106.0 | 60.256/px | 9,238.1 | 1.4 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 7,559.3 | 0.845/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 385.8 | 16.553/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 9,642.6 | 0.662/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,467.7 | 4.351/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 9,816.5 | 0.650/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 4,116.2 | 1.551/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltPattern.800x600 | HW | 10,168.1 | 0.628/px | 9,238.1 | 1.1 | D3 |
| Gdi.PatBltPattern.8x8 | CPU | 106.0 | 60.239/px | 9,238.1 | 1.1 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 9,133.5 | 0.699/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.16x16 | CPU | 570.3 | 11.196/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.256x256 | CPU | 4,435.4 | 1.440/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,261.3 | 2.824/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 7,214.1 | 0.885/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.64x64 | CPU | 5,404.5 | 1.181/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.800x600 | CPU-fast | 9,346.2 | 0.683/px | 9,238.1 | 1.8 |  |
| Gdi.PatBltSolid.8x8 | CPU | 160.7 | 39.737/px | 9,238.1 | 1.8 |  |
| Gdi.Polygon.128x128 | CPU | 1,153.4 | 5.536/px | 9,238.1 | - |  |
| Gdi.Polyline | n/a | - | 685.625/line | 9,238.1 | - |  |
| Gdi.Rectangle.128x128 | CPU | 3,278.2 | 1.948/px | 9,238.1 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,545.8 | 2.508/px | 9,238.1 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.6 | 259.064/px | 9,238.1 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,526.5 | 2.527/px | 9,238.1 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.9 | 428.054/px | 9,238.1 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.9 | 427.430/px | 9,238.1 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.7 | 258.324/px | 9,238.1 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.9 | 427.205/px | 9,238.1 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,534.6 | 2.519/px | 9,238.1 | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,818.9 | 3.510/px | 9,238.1 | - |  |
| Gdi.ScrollBitBlt | CPU | 148.8 | 42.912/px | 9,238.1 | - |  |
| Gdi.ScrollDC | CPU | 148.6 | 42.972/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.400x300 | CPU | 4,052.3 | 1.576/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.800x600 | CPU | 4,206.5 | 1.518/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.400x300 | CPU | 4,080.2 | 1.565/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 | CPU | 4,230.9 | 1.509/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.400x300 | CPU | 735.5 | 8.682/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.800x600 | CPU | 741.4 | 8.612/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.400x300 | CPU | 735.0 | 8.688/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.800x600 | CPU | 741.7 | 8.609/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.400x300 | CPU | 3,230.1 | 1.977/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.800x600 | CPU | 3,308.4 | 1.930/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.400x300 | CPU | 3,233.3 | 1.975/px | 9,238.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.800x600 | CPU | 3,339.1 | 1.912/px | 9,238.1 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 17.4 | 366.100/px | 9,238.1 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 7.2 | 890.050/px | 9,238.1 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 16.9 | 378.856/px | 9,238.1 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 7.2 | 890.489/px | 9,238.1 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 80.1 | 79.693/px | 9,238.1 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 85.0 | 75.159/px | 9,238.1 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 80.1 | 79.696/px | 9,238.1 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 84.9 | 75.197/px | 9,238.1 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 181.4 | 35.195/px | 9,238.1 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 230.8 | 27.670/px | 9,238.1 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 181.7 | 35.142/px | 9,238.1 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 230.6 | 27.687/px | 9,238.1 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 20,717.819/string | 9,238.1 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 15,477.479/string | 9,238.1 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 23,585.639/string | 9,238.1 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 19,378.566/string | 9,238.1 | - |  |

## CLAIMED-NOT-DELIVERED

- Dd.Blt.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.16x16: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU

## Problems

- DBGDI: truncated report
