# Cylindrical wrap through S3: both 3D WinBench 98 wrap tests pass on the netbook

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, boot 6. Driver
0.8.0 `intel-gma` with only `V9XHAL.DLL` replaced by a build of this change
(100,864 bytes), swapped in by WININIT.INI rename.

## The defect

3D WinBench 98's Cylindrical Wrap u and v tests drew the frame the bad
reference shows: the seam of the texture interpolated the long way round
the whole cylinder, a streak across it and the 9 and 0 unreadable
(`2026-09-24-netbook-3dwb98-wrap-u-incorrect.png`). Two faults stacked:

- `d3d_i9xx.c` never read `D3DRENDERSTATE_WRAPU` or `WRAPV`.
- The core stored both states in one field, `texture_wrap`, so the engine
  could not have told the two axes apart. (That field is the ViRGE's "either
  is set", and is left as it was.)

## The change - in hardware

Gen3's LOAD_STATE_IMMEDIATE_1 S3 dword carries, per texture-coordinate set,
a WRAP_SHORTEST bit for each coordinate: interpolate the short way round
the unit interval, which is exactly Direct3D's cylindrical wrap.

- `include\velocity9x\intel_gen3_3d.h`: `V9X_I9XX_S3_WRAP_SHORTEST_TCX0`
  `0x8` and `_TCY0` `0x4`, and the request flags `V9X_I9XX_CYLINDER_U`/`_V`.
- `i9xx_3d.c`: `v9x_i9xx_build_runtime_state` takes a `cylinder` request,
  refuses it without a texture or with any other bit, and translates it to
  S3; the pipeline emitter's S3 is a parameter, zero for every scene.
- `i9xx_3d_decode.c`: **S3 was not checked at all before this** - any value
  passed the allowlist. It is now held equal to what the limits' new
  `texture_cylinder` field declares, zero for every scene and untextured
  stream. That also closes `PERSPECTIVE_DISABLE`, which a stray S3 bit
  would have set silently.
- `d3d_core.c` / `d3d_internal.h`: `wrap_u` and `wrap_v`, per axis, default
  FALSE (Direct3D's default).
- `d3d_i9xx.c`: requests the wrap for the axes the application set,
  textured draws only.

## Sources, and what they do not settle

Two trees define the encoding identically:

- xf86-video-intel `src\sna\gen3_render.h:350-358` -
  `TEXCOORD_WRAP_SHORTEST_TCX 8`, `_TCY 4`, shifted by `unit * 4`.
- Mesa 21.3 classic `src\mesa\drivers\dri\i915\intel_reg.h:97-100` -
  `S3_TEXCOORD_WRAP_SHORTEST_TCX(unit) (1<<((unit)*4+3))`, `_TCY +2`.

Neither USES the wrap bits (GL has no cylindrical wrap), so the packet
audit's two-use-site rule is not met; recorded as a judgement in the
header. The nibble layout is used: Mesa's `i915_fragprog.c:1306` sets
`S3_TEXCOORD_PERSPECTIVE_DISABLE`, bit 0 of the same nibble. The Mesa
gallium i915 header omits S3 ("S3 not interesting"). All three read
through the built-in browser from gitlab.freedesktop.org, 2026-09-25.

The same pass confirmed the S4 cull encodings in two trees (gallium
`i915_reg.h` and `sna\gen3_render.h:371-375`: BOTH 0, NONE 1, CW 2, CCW 3
at bit 13), for `docs\issues\2026-09-25-intel-culling-is-done-in-software.md`.

## Measured

Host: the new `test_runtime_cylindrical_wrap` in `test_i9xx_3d.c` checks the
S3 dword, per-axis bits, decoder equality in both directions, a stray
PERSPECTIVE_DISABLE bit, and the two builder refusals. Mutation check: with
the emitter forced back to S3 = 0 the test fails four assertions; restored,
it passes. The ten existing runtime-builder test calls pass the new
argument as zero. Full gate `run-checks.ps1` green; the generated scene
CRCs are unchanged (Phase 5 `01A4DE25`, combined `1229FE1F`), as they must
be with S3 still zero for every scene.

Netbook, overrides at DEFAULT (`2026-09-25-netbook-3dwb98-wrap-check.txt`),
"Direct3D HAL was used" on every row:

| test | before | now |
|---|---|---|
| 12 Modulate Texture Blending | Capable | Capable |
| 16 Flat Wrap Texture Addressing | Capable | Capable |
| 17 Cylindrical Wrap u | Incorrect | **Capable** - "89012" legible, no streak (`...-wrap-u-pass.png`) |
| 18 Cylindrical Wrap v | Incorrect | **Capable** (`...-wrap-v-pass.png`) |
| 19 Clamp Texture Addressing | Capable | Capable |

So the encoding the two trees define is the one this part honours for
coordinate set 0. Verdicts are from the benchmark's captured last frame;
the panel was not watched.

## Not measured

- Wrap on a coordinate set other than 0 - the engine uses one set.
- Wrap together with perspective-corrected, mipmapped or linear-filtered
  draws beyond what these tests draw.
- Any application other than the benchmark.
