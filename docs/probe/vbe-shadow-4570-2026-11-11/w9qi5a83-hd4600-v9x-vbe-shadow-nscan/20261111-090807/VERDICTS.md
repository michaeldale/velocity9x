# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-nscan

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x16, build a80e784, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 11,313.2 |
| FbRead | 227.5 |
| MemFill | 31,057.5 |
| MemCopy | 18,883.5 |
| DibToDibBlt | 8,653.3 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 2,338,906.3 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 60.0 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 104 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 892.6 | 7.153/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,794.8 | 3.558/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,449.7 | 0.611/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,415.5 | 4.511/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,219.1 | 2.877/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,960.5 | 0.534/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,655.2 | 0.436/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,290.9 | 0.620/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,923.1 | 3.320/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 14,231.4 | 0.449/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,679.5 | 0.383/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 9,098.3 | 0.702/px | - | 0.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 444.9 | 14.353/px | 11,313.2 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 1,741.5 | 3.666/px | 11,313.2 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 5,915.8 | 1.079/px | 11,313.2 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 1,278.4 | 4.995/px | 11,313.2 | 17.4 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 545.6 | 11.703/px | 11,313.2 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 13,386.3 | 0.477/px | 11,313.2 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 16,386.6 | 0.390/px | 11,313.2 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 6,314.3 | 1.011/px | 11,313.2 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 579.5 | 11.019/px | 11,313.2 | 0.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 17,411.3 | 0.367/px | 11,313.2 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 18,122.1 | 0.352/px | 11,313.2 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 5,975.0 | 1.069/px | 11,313.2 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 33.9 | 188.216/px | 227.5 | -2.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.141/px | 227.5 | -2.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.576/px | 227.5 | -2.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.674/px | 227.5 | -2.2 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 64.2 | 99.482/px | 227.5 | -1.4 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 70.8 | 90.158/px | 227.5 | -1.4 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.6 | 90.459/px | 227.5 | -1.4 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 70.5 | 90.525/px | 227.5 | -1.4 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 120.3 | 53.071/px | 227.5 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 142.0 | 44.962/px | 227.5 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 142.7 | 44.751/px | 227.5 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 141.5 | 45.114/px | 227.5 | 1.7 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 23.9 | 266.910/px | 11,313.2 | -0.3 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.8 | 257.652/px | 11,313.2 | -0.3 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.8 | 257.815/px | 11,313.2 | -0.3 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.7 | 258.439/px | 11,313.2 | -0.3 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 23.5 | 271.243/px | 11,313.2 | 3.1 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 23.9 | 267.437/px | 11,313.2 | 3.1 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU | 23.9 | 266.939/px | 11,313.2 | 3.1 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 23.9 | 267.197/px | 11,313.2 | 3.1 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 90.4 | 70.608/px | 11,313.2 | 3.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 114.1 | 55.971/px | 11,313.2 | 3.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 114.9 | 55.556/px | 11,313.2 | 3.8 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 111.4 | 57.325/px | 11,313.2 | 3.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,518.3 | 2.536/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,329.3 | 0.564/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,471.8 | 0.441/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,665.4 | 0.599/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 365.4 | 17.473/px | 11,313.2 | 1.8 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU | 12,017.4 | 0.531/px | 11,313.2 | 1.8 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 16,104.5 | 0.396/px | 11,313.2 | 1.8 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 4,614.6 | 1.384/px | 11,313.2 | 1.8 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 665.3 | 9.597/px | 227.5 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 10,663.6 | 0.599/px | 227.5 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,209.5 | 0.449/px | 227.5 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 6,088.5 | 1.049/px | 227.5 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 23.6 | 270.233/px | 11,313.2 | 3.8 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 23.9 | 267.707/px | 11,313.2 | 3.8 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU | 23.9 | 267.112/px | 11,313.2 | 3.8 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 23.9 | 267.558/px | 11,313.2 | 3.8 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 458.4 | 13.929/px | 11,313.2 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 1,704.1 | 3.747/px | 11,313.2 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 5,889.2 | 1.084/px | 11,313.2 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 1,256.7 | 5.081/px | 11,313.2 | 17.8 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,775.1 | 2.301/px | - | 0.6 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 15,364.3 | 0.416/px | - | 0.6 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 18,887.0 | 0.338/px | - | 0.6 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,423.7 | 0.414/px | - | 0.6 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 398.8 | 16.010/px | 11,313.2 | 2.1 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 7,143.0 | 0.894/px | 11,313.2 | 2.1 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 8,768.7 | 0.728/px | 11,313.2 | 2.1 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 3,232.1 | 1.976/px | 11,313.2 | 2.1 | claimed |
| Dd.Lock.Back | n/a | - | 1,981.664/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 1,992.363/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 1,987.191/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 563.862/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 106,113.398/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 753.8 | 8.471/px | 11,313.2 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 785.1 | 8.133/px | 11,313.2 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 787.2 | 8.112/px | 11,313.2 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 786.8 | 8.116/px | 11,313.2 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 789.3 | 8.090/px | 11,313.2 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 785.6 | 8.127/px | 11,313.2 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,950.1 | 0.919/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,544.9 | 1.801/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 336.6 | 18.970/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,121.2 | 2.046/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,078.1 | 5.923/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 4,980.0 | 1.282/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,540.7 | 2.513/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 97.3 | 65.611/px | 11,313.2 | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,144.9 | 2.030/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,287.3 | 2.792/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 202.7 | 31.505/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,079.3 | 3.071/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 648.9 | 9.840/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,720.7 | 2.347/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,564.2 | 4.082/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 56.7 | 112.562/px | 11,313.2 | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | HW? | 6,994.5 | 0.913/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.128x128 | HW? | 3,496.6 | 1.826/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.16x16 | HW? | 337.3 | 18.930/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.256x256 | HW? | 3,124.2 | 2.044/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.32x32 | HW? | 1,067.0 | 5.984/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.512x512 | HW? | 4,838.4 | 1.320/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.64x64 | HW? | 2,539.3 | 2.515/px | 227.5 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.8x8 | HW? | 97.6 | 65.393/px | 227.5 | 3.4 | D4 |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,120.0 | 5.701/px | 11,313.2 | 7.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 121.5 | 52.539/px | 11,313.2 | 7.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,123.7 | 3.007/px | 11,313.2 | 7.2 | D2 -0.07 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 284.9 | 22.416/px | 11,313.2 | 7.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 607.5 | 10.511/px | 11,313.2 | 7.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.8 | 130.909/px | 11,313.2 | 7.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,179.3 | 2.008/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 244.1 | 26.158/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,665.7 | 2.395/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 789.5 | 8.088/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,855.7 | 1.656/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,995.0 | 3.201/px | 11,313.2 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 88.3 | 72.346/px | 11,313.2 | 3.7 |  |
| Gdi.Ellipse.128x128 | CPU | 633.9 | 10.074/px | 11,313.2 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,781.488/string | 11,313.2 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 18,848.080/string | 11,313.2 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,294.388/string | 11,313.2 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 18,842.536/string | 11,313.2 | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,591.6 | 0.841/px | 11,313.2 | 3.9 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,608.7 | 1.769/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.16x16 | CPU | 265.4 | 24.063/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.256x256 | CPU | 3,203.0 | 1.994/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.32x32 | CPU | 878.0 | 7.273/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.512x512 | CPU | 5,219.9 | 1.223/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.64x64 | CPU | 2,253.6 | 2.833/px | 11,313.2 | 3.9 |  |
| Gdi.FillRect.8x8 | CPU | 71.5 | 89.284/px | 11,313.2 | 3.9 |  |
| Gdi.LineDiagonal30 | n/a | - | 9,304.309/line | 11,313.2 | - |  |
| Gdi.LineHorizontal | n/a | - | 4,563.596/line | 11,313.2 | - |  |
| Gdi.LineVertical | n/a | - | 10,363.622/line | 11,313.2 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,290.3 | 1.207/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,602.2 | 1.773/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.16x16 | CPU | 284.5 | 22.446/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,906.9 | 2.197/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.32x32 | CPU | 920.8 | 6.935/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,043.9 | 1.579/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,348.6 | 2.719/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltHatch.8x8 | CPU | 82.4 | 77.497/px | 11,313.2 | 3.0 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,291.0 | 1.207/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,593.9 | 1.777/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 284.6 | 22.437/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,909.7 | 2.194/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 920.3 | 6.939/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,113.3 | 1.552/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,348.3 | 2.719/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 82.4 | 77.490/px | 11,313.2 | 3.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,635.1 | 0.836/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,914.3 | 1.631/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 383.9 | 16.634/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,256.5 | 1.961/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,183.6 | 5.395/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,295.9 | 1.206/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,723.1 | 2.345/px | 11,313.2 | 3.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.4 | 56.807/px | 11,313.2 | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 701.8 | 9.099/px | 11,313.2 | - |  |
| Gdi.Polyline | n/a | - | 824.297/line | 11,313.2 | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,290.4 | 2.788/px | 11,313.2 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,291.5 | 2.786/px | 11,313.2 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 574.5 | 11.114/px | 11,313.2 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,276.5 | 2.805/px | 11,313.2 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 540.6 | 11.810/px | 11,313.2 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 559.5 | 11.413/px | 11,313.2 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 607.7 | 10.507/px | 11,313.2 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 560.1 | 11.401/px | 11,313.2 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,291.6 | 2.786/px | 11,313.2 | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,327.4 | 4.810/px | 11,313.2 | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,011.4 | 0.911/px | 11,313.2 | - |  |
| Gdi.ScrollDC | CPU-fast | 6,957.9 | 0.918/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,062.8 | 2.085/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,671.8 | 2.390/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,074.7 | 2.077/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,636.8 | 2.422/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 694.5 | 9.194/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 671.8 | 9.504/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 694.8 | 9.190/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 669.2 | 9.542/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,550.8 | 2.503/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,276.9 | 2.804/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,573.0 | 2.482/px | 11,313.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,254.1 | 2.833/px | 11,313.2 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.8 | 160.233/px | 11,313.2 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.2 | 181.464/px | 11,313.2 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 39.8 | 160.420/px | 11,313.2 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.8 | 178.514/px | 11,313.2 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 174.8 | 36.527/px | 11,313.2 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 203.9 | 31.317/px | 11,313.2 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 175.4 | 36.396/px | 11,313.2 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 204.4 | 31.240/px | 11,313.2 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 173.4 | 36.829/px | 11,313.2 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 224.7 | 28.412/px | 11,313.2 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 173.3 | 36.839/px | 11,313.2 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 224.0 | 28.511/px | 11,313.2 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,665.728/string | 11,313.2 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,542.223/string | 11,313.2 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,186.691/string | 11,313.2 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 18,728.626/string | 11,313.2 | - |  |

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
