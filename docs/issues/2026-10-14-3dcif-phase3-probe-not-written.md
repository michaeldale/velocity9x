# 3DCIF: the Phase 3 readback probe is not written

Date: 2026-10-14

Status: Open, parked by Michael on 2026-10-14.

[ati-3dcif-wrapper.md](../plans/ati-3dcif-wrapper.md), Phase 3, gates
on a probe, `tools/diag/cif_probe_win32.c`:
- It creates its own DirectDraw chain, as a 3DCIF title does.
- It draws flat, Gouraud, textured, blended and depth-tested primitives
  through ATI3DCIF.DLL.
- It locks the back buffer and logs the pixels.

The gate is that readback, matched between the software engine and the
Rage XL, with every difference explained. The same goes for the
netbook's Gen3.

Until it exists, ATI3DCIF.DLL is judged by its log, the DLL's centre-pixel
readback and Michael's eyes on the eight SDK demos. The conventions
Phase 2 took without a measurement are still unchecked:
- no pixel-centre offset;
- a non-mip filter on a mipmapped texture picking the nearest level;
- the chroma key compared in the texel's own precision;
- exclusive bottom-right corners for the viewport and scissor.

The probe is the place to measure them.
