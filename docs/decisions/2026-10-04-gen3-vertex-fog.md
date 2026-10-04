# Vertex fog on Gen3, in the fragment program, drawn on the netbook

Date: 2026-10-04. Machine: MICHAEL-NETBOOK (945GSE, 1024x576x16). Follows
`docs/decisions/2026-10-04-netbook-3dmark-untextured-draws-are-a-fog-pass.md`.
Evidence: `docs/probe/netbook-vertex-fog-2026-10-04/`.

## What was built

The Intel device now advertises `D3DPRASTERCAPS_FOGVERTEX` and
`D3DPSHADECAPS_FOGFLAT | FOGGOURAUD`, and draws vertex fog. It does so
the way Mesa's i915 driver does fog: in the fragment program, not
through the fixed-function fog unit. Mesa's context comment says the
fixed-function unit conflicts with fog in the program
(`i915_context.c`, mesa-20.3.5).

- **Vertex.** A fog draw's vertex carries the Direct3D specular dword
  after the diffuse, declared by `S4_VFMT_SPEC_FOG` (bit 11, Mesa's
  `intel_reg.h`). Its alpha is Direct3D's fog factor, 1.0 meaning no
  fog. The vertex is six dwords untextured and eight textured.
- **Constant.** The fog colour goes in C0, loaded by
  `_3DSTATE_PIXEL_SHADER_CONSTANTS` (`0x7d060004`, mask 1, r g b 1.0).
  The layout is from Mesa's `i915_reg.h` and is corroborated by
  libdrm's `intel_decode.c`.
- **Program.** Each existing program has a fog form (`i9xx_fog.c`).
  Gen3 has no LRP; Mesa lowers it the same way:

      mad R2, -T9.wwww, C0, C0          (1 - f) * fog colour
      mad oC.xyz, T9.wwww, colour, R2
      mov oC.w, alpha

  The colour is T8, the texel R0, or their product R1, depending on the
  program. One constant register per instruction is the hardware rule,
  and C0 read twice is one register.
- **Decoder.** A runtime stream with `limits.fog` must carry all four
  parts: SPEC_FOG in S4, exactly one C0 packet with components in
  [0, 1] ahead of the program, the fog program's length, and the longer
  stride. A stream without `fog` may carry none of them, and a scene may
  never carry fog.
- **Submit buffer.** Raised from 1,536 to 2,048 dwords, because 64
  fogged textured triangles are 1,537 dwords of primitive alone.
- **I9XXCODE.** The builders are HAL-only, so they live in `i9xx_fog.c`,
  which the 16-bit driver does not link; its I9XXCODE segment went 61
  bytes over budget when they were in `i9xx_fragprog.c`.
  `v9x_i9xx_build_runtime_state` became a macro over the `_fog` form for
  the same reason. The second decode site in `intel_3d16.c` now clears
  its limits, as the first already did, so a stack value in an appended
  field cannot refuse a correct scene.

Host test `test_runtime_fog` checks the untextured program dword for
dword against the encodings worked by hand from Mesa. It also passes a
64-triangle fogged textured stream through the decoder, and has the
decoder refuse that stream against no-fog limits, inside a scene, and
without its constants.

## Measured on the netbook

3DMark 99, one boot. Counters are deltas per run.

| Run | Batches | Untextured | Blended untextured | Engine refusals |
|-----|--------:|-----------:|-------------------:|----------------:|
| Game 1, before (census build) | 50,670 | 19,696 | 19,438 | 72 |
| Game 1, fog, first run | 36,839 | 304 | 0 | 55 |
| Game 1, fog, second run | 36,357 | 300 | 0 | 44 |
| Game 2, fog | 55,224 | 0 | 0 | 409 |

- The fog pass is gone. About 300 untextured batches remain per Game 1
  run, all with blending off and varying coordinates; the Rage XL sends
  173 such. That is 3DMark's own, not looked into.
- **Refusals.** All are reason 6 (VERTICES, a vertex outside the
  rectangle or its depth range). They appear at the same rate in the
  pre-fog runs: 72 in Game 1, and about 400 in Game 2 before fog. Fog
  added none. No decoder rejection, ring timeout or flip-wait timeout.
- **Frames.** Captured with `V9XTRACE -arm`, title screens off:
  - Game 1, flip 390: fully textured, with sky, stands, track, cars
    ahead and the HUD, and grey haze toward the horizon.
  - Game 2, flip 779: the corridor, its lighting, the weapon and the
    HUD.
  - The first run's capture was flip 1, blank, taken before the scene
    drew. The capture samples every ~98th flip from the session's
    first, so the first run after a boot catches the opening flip.
- **Speed.** Game 1 reported 17.6 fps; the census build reported 14.8
  for the same test. Recorded only, not compared or chased.

## Not established

- Only 128x72 thumbnails were seen; nobody watched the panel. Whether
  the fog's depth gradient matches the Rage XL's or the reference
  image's is unchecked.
- Table fog (FOGTABLEMODE) is not advertised and not drawn. An
  application that sets it gets vertex fog from whatever its specular
  alpha holds.
- Specular colour (SPECULARENABLE) is still not added; only the alpha
  is used.
- The OpenGL ICD never sets `fog_enable` (`gl_prim.c`), so GL fog is
  unaffected.
