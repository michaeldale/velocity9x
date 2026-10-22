# GL_ARB_multitexture in the ICD

Date: 2026-10-22. Status: step 1 done (HD 4600, 2026-10-22: Quake 3
looks for `GL_ARB_multitexture` among six extensions and finds none,
[issue record](../issues/2026-10-22-github-11-quake3-glitches-intel.md));
the issue's floor glitch turned out to be a blend factor, not the
two-pass path. Steps 2 and 3 coded and host-tested for 0.25.0
(`run-checks` green, golden pipeline hash unmoved). Step 4 done and run
on the netbook, and step 5's Quake 3 run there: 14.8 fps with the
extension, 13.1 without
([record](../decisions/2026-10-22-netbook-arb-multitexture-quake3.md)).
Half-Life on the netbook takes the ARB path and runs mwd5 as fast as on
SGIS ([record](../decisions/2026-10-22-netbook-hl1-arb-vs-sgis.md)).
Owed: the HD 4600, screenshots of the issue's views, Quake 2, and
Half-Life on A8U4I5. Michael's request, from
[GitHub issue 11](https://github.com/michaeldale/velocity9x/issues/11)
(Quake 3 demo 1.11 glitches on the GMA 900 and the HD 4000, 0.23.0).
Follows [gen3-sgis-multitexture.md](gen3-sgis-multitexture.md), which
put `GL_ARB_multitexture` out of scope because the Quake census of
2026-09-26 found no title asking for it.

## Why

Quake III Arena asks for `GL_ARB_multitexture`, not
`GL_SGIS_multitexture`. This is from id's GPL source (`GLW_InitExtensions`
in `code/win32/win_glimp.c`) and is to be confirmed by step 1, not yet
measured on our ICD. Without it the renderer draws every lightmapped
surface in two passes. The ICD offers only SGIS today, so Quake 3 on
Velocity9x always takes the two-pass path.

Issue 11's glitches have not been tied to that path. A plain wall emblem
showing a ghost of a neighbouring texture, and floor and wall patches
drawn unlit, fit a second pass blended wrongly, but nothing has been run.
This plan adds the extension; whether it also clears the glitches is
for its gate to show.

## What the extension adds over SGIS

Tokens and entry points (ARB_multitexture spec, the ARB's 1998 text):

- `GL_TEXTURE0_ARB` 0x84C0 to `GL_TEXTURE31_ARB`; `GL_ACTIVE_TEXTURE_ARB`
  0x84E0, `GL_CLIENT_ACTIVE_TEXTURE_ARB` 0x84E1,
  `GL_MAX_TEXTURE_UNITS_ARB` 0x84E2 (answered from describe's
  `texture_units`).
- `glActiveTextureARB`, `glClientActiveTextureARB`, and the 32
  `glMultiTexCoord{1,2,3,4}{s,i,f,d}[v]ARB` forms, through
  `DrvGetProcAddress`.
- **The difference from SGIS that matters:** per-unit client state.
  `glClientActiveTextureARB` selects which unit `glTexCoordPointer`,
  `glEnableClientState(GL_TEXTURE_COORD_ARRAY)` and the array queries
  address, so `glDrawElements` and `glArrayElement` (gl_varray.c) must
  fetch both units' coordinates. Quake 3 draws through vertex arrays, so
  this is the path that counts. SGIS needed it only for immediate mode.
- Texture binding, enables, env mode and colour per active unit: already
  per unit for SGIS; the ARB names select the same state.
- `GL_TEXTURE_BIT` push/pop and the queries per unit, as the spec's
  state table lists them.

The render interface already carries a second unit (ABI 4's `texture1`
and `texcoords1`). **No ABI change.** Engines whose describe says two
units take it: the software engine, Gen3, and others as their
`texture_units` says. One-unit engines keep the extension unadvertised.

## Steps

1. **Census.** Run the Quake 3 demo on the HD 4600 as it is and record
   what it asks for. The renderer prints each extension it looks for,
   found or not, to its console (`condump` writes it out). V9XGL.LOG
   names every `DrvGetProcAddress` it makes and the first call of each
   stub. That makes the "Why" a measurement, and shows whether it also
   wants `GL_EXT_compiled_vertex_array`, `GL_EXT_texture_env_add` or
   `WGL_EXT_swap_control`. Record its default `r_ext_*` cvars. Issue
   11's two screenshots are the reference views, on the demo's first
   map.
2. **ICD, host-tested first.** The tokens, `glActiveTextureARB` and
   `glClientActiveTextureARB` over the SGIS unit state, the
   `glMultiTexCoord*ARB` forms, the per-unit texture-coordinate arrays in
   `gl_varray.c`, and the queries and attribute stacks. The golden
   pipeline hash must not move for one-unit draws.
3. **`GL_ARB_multitexture` in `GL_EXTENSIONS`** where describe says two
   units, beside SGIS, under the 4 KB limit and without the Quake
   substrings the renderer string avoids.
4. **V9XGLP section:** the SGIS section's env-mode quads again through
   the ARB entry points and through two-unit vertex arrays, read back,
   exact against the software engine. Done as `Arb<way><case>` keys
   (immediate, `glDrawArrays`, `glDrawElements`), checked against the
   SGIS section's expected colours; not yet run on the software engine.
5. **Gate:** the Quake 3 demo on the HD 4600 and the netbook, at the
   issue's preset (640x480, 16 bpp), with `r_ext_multitexture 1` and 0.
   Screenshots at issue 11's two views, compared with the issue's.
   Scores recorded, not compared as targets.

## Notes from steps 2 and 3

- `V9X_GL_ARRAYS` gained `texcoord1` and `client_active` beside the six
  arrays, so `glPushClientAttrib`'s copy of the struct carries them, as
  the spec puts both in the vertex-array group.
- Unit 1 still carries s and t only (`V9X_GL_VERTEX.tex1`): r and q from
  `glMultiTexCoord3/4` or a 3- or 4-component unit 1 array are dropped,
  and `CURRENT_TEXTURE_COORDS` reads them back as 0 and 1.
- `glDrawArrays` and `glDrawElements` now set the pipeline's unit count
  as `glBegin` does. Before, an array draw carried unit 1 only if the
  last `glBegin` had; with SGIS nothing fed unit 1 from arrays, so it did
  not matter.
- `MAX_TEXTURE_UNITS_ARB` answers 2 where the extension is offered and 1
  elsewhere, rather than INVALID_ENUM.
- **Risk for step 5:** Quake 2 3.20 takes `GL_ARB_multitexture` over
  SGIS when both are offered (id's GPL source, `R_Init`; not measured on
  our ICD). Half-Life's order is not known. Whichever moves to the ARB
  entry points is no longer covered by its SGIS measurements; the gate
  must rerun Quake 2 and Half-Life on the netbook and A8U4I5 beside
  Quake 3, reading V9XGL.LOG's `arb first` lines for which path each
  took.

## Not in scope

More than two units. `GL_EXT_texture_env_combine`. A separate texture
matrix per unit (not applied for unit 0 either). The issue's two-pass
glitch, if step 5 shows it survives the extension: that is issue 11's
own investigation.
