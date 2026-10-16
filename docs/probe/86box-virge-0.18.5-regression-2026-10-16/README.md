# 86Box ViRGE/DX: the 0.18.5 build, before release

Date: 2026-10-16. Guest `WIN98-86BOX` (86Box `virge_dx_pci`, port 9869),
boots 669 to 670. Why: `5a732f78` makes the 5:5:5 mode choice search the
BIOS's own mode list for a 15 bpp twin. That code is not ATI-specific, and
the ViRGE is the other family that runs 5:5:5 under hardware Direct3D.

Before: the driver from `9a4c4473-dirty` (boot 669). After: `V9XDISP.DRV`,
`V9XMINI.VXD`, `V9XHAL.DLL` and `V9XSETP.DLL` from the `s3` package of
`a233cfad`, by WININIT rename; all four sizes equal the package.

- **Desktop:** `enable-ok`, `ColourLayout=555-auto`, `Colour=hc=15 ini=0
  row=279 set=278 is555=1 mask=31744`, the same as before.
  `desktop-after.png` is clean.
- **Mode list:** `V9XMODES.INI` before and after differ only in `Build`
  and `Generation`. This BIOS lists no extra 15 bpp twins, so the new
  search changes nothing here.
- **V9XDDP:** 49 of the comparable `*Ok` keys pass before and after, and
  the Direct3D target stays 5:5:5 (`D3DTargetRMask=31744`). One key
  differs: `D3DDevice2HwTriRaster` 432 to 176, the table-fog bit dropped
  by `bd7584d7`, which shipped in 0.18.4. The new probe's
  `D3DFogMulOk` reads 0 (all three raws 65535); the ViRGE's fog is an
  open issue (`2026-10-15-virge-claims-vertex-fog-it-does-not-draw.md`).

No game was run; the 3DCIF changes do not reach the ViRGE's own path.
