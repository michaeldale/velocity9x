# DxDiag shows the display driver's version as "()"

Date: 2026-10-03. Reported by MarxVeix on an ATI 3D Rage XL AGP
(0.10.0). Evidence:
`docs/probe/rage-xl-agp-marxveix-2026-10-03/dxdiag-display.png`.
Status: open, cosmetic, cause not checked.

## Symptom

DxDiag's Display page lists `Main Driver: v9xdisp.drv` with
`Version: ()`, `Certified: No`. The rest of the page is right: the
name, 8 MB, the mini-VDD, and DirectDraw and Direct3D acceleration
enabled with their tests passing.

## Suspect

V9XDISP.DRV carries no version resource, or not one in the form DxDiag
reads. Not checked against the build scripts.

## Next

Read V9XDISP.DRV's resources from a build. If none is there, decide
whether to add one, stamped from `include/velocity9x/build.h`.
