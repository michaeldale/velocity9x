# The ViRGE claims vertex fog it does not draw on the DrawPrimitive path

Date: 2026-10-15

Status: Open, not new. Found while measuring table fog on the 86Box
ViRGE/DX (Win86SE, boots 667-668). Evidence:
`docs/probe/table-fog-other-engines-2026-10-15/virge-*`.

The ViRGE claims `D3DPRASTERCAPS_FOGVERTEX`, `FOGFLAT` and `FOGGOURAUD`.
The claim was added for Hellbender, which warns about fog without it.

The S3D unit has no fog. The driver folds fog into the vertex colour in
`v9x_d3d_apply_vertex_color`. That runs only on the execute-buffer path
(`v9x_d3d_render_primitive_body`), which Hellbender uses. DrawPrimitive and
DrawPrimitives2 batches reach the ViRGE engine unfogged.
`v9x_d3d_virge_accepts` refuses a render-interface draw with fog for the
same reason.

| Key | Boot 667 | Boot 668 |
|---|---|---|
| `D3DDepthFogOk` (untextured, factor 0) | 0 | 0 |
| `D3DFogTexOk` (textured, three factors) | 0 | 0 |

The table-fog cap was claimed for one build on top of this and drew
nothing either. It is no longer claimed on this engine.

Next: either fold vertex fog into the colour in the core for every batch
on an engine without hardware fog, which the factor makes possible; or
claim fog only where it is drawn. Measure against Hellbender before
dropping its caps.
