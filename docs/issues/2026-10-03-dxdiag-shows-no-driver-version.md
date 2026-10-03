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

## Fixed (2026-10-04)

There was one, unreadable. In wrc's 16-bit output, values ending in the
`.rc` text's explicit `\0` declared one byte more than was written.
InternalName and OriginalFilename each crossed a 4-byte boundary with
it, 8 bytes in all, and `GetFileVersionInfo` rejected the resource.
Without the `\0`, wrc writes no terminator at all, so the build now lays
out the 16-bit block itself and hands wrc raw data. It also fails when
Windows cannot read the version. DxDiag on A8U4I5 now reads
`Driver Version: 0.10.0000.0000 (English)`. `Mini VDD Date` is still
blank, separately.
`docs/decisions/2026-10-04-rage-xl-small-textures-sync-version-and-oversize.md`.
