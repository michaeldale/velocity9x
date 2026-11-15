# RV280 OpenGL: a band of the first depth-tested frame fails its test

Date: 2026-11-15

Status: open.

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE, branch
`ati-rv280`, boots 23 to 26. Probe: V9XGLP.

## What is seen

V9XGLP's geometry check runs first among its depth-tested checks:
- colour and depth are cleared;
- a green quad covers the whole 360-pixel-wide window at window z 0.5,
  with LESS;
- then a blue quad at 0.75 covers the left half, and a red one at 0.25
  covers the right half.

The scan of the back buffer reads black at samples 8 and 9 (x 58 and 65)
on all three scanned rows, and green on either side (x 51 and 72):

```
GeomScanRow25=GGGGGGGGKKGGGGGGGGGGGGGGGRRRRRRRRRRRRRRRRRRRRRRR
```

So a band about 8 to 20 pixels wide, somewhere between x 52 and 71,
failed the depth test, or was never drawn, over the whole height.
- Windows' GDI Generic renderer reads all green on the same machine
  (boot 22). So do the Carrizo and the HD 2600 XT.
- V9XGLP's later depth-tested checks (offset, fog, TnL) pass.
- V9XGLP's overall `Result` is PASS: GeomScan is reported, not judged.

## What is ruled out

- **The scissor.** A build with the scissor off for every
  render-interface draw read the same band (boot 24).
- **Stale memory.** Two runs had the depth buffer at different offsets
  and read the band at the same x.
- **The Z buffer's alignment.** It was placed on 4 KiB, as the X driver
  places the R200's (boot 26). The band did not move. The alignment is
  kept, because it matches the reference.
- **The R200's depth at this shape.** V9XR23D's WideZCpu and WideZ2D
  checks were exact at 16 and 32 bpp. Both draw a 360 x 32 target over a
  384-pixel-pitch Z buffer on 4 KiB, set to FFFFh by the CPU or by the 2D
  engine over the pitch and all rows (as `eng_rv280.c`'s depth fill
  does), then one green quad at z 0.5 with LESS.

## Not yet looked at

- The ICD's own sequence before the first depth draw: the scissored
  colour clear, then the full colour-and-depth clear.
- Whether the depth fill ran, or was declined, for that clear.
- The pixel values in the band.
- Whether the band follows the window's width.
