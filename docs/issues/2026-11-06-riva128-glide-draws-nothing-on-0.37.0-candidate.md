# M-7V636C (RIVA 128ZX): the Glide 2 probe draws no triangles on the 0.37.0 candidate

Date: 2026-11-06. Machine: M-7V636C, RIVA 128ZX (12D2:0018), nvidia
package, desktop 800x600. Found in the 0.37.0 release checks. Michael:
review in a later release.

## What was seen

| Probe | Installed driver, boot 39 (build not identified) | 0.37.0 candidate `55a661a2`, boot 40 |
|---|---|---|
| V9XD32 `/bpp16` | 1 failure (T21) | 1 failure (T21) |
| V9XGLP | PASS | PASS |
| V9XGLIDP | FAIL, 1 (Line) | **FAIL, 14** |
| V9XR3DP | COMPLETE | COMPLETE |

On the candidate, Glide's open, clears, flip and front-buffer checks
pass. Every triangle, line, textured, chroma-keyed, blended and fogged
check reads the clear colour or 0000. That is the same pattern as the
RS690, whose engine refuses render-interface draws and has no software
fallback (`docs/decisions/2026-11-06-rs690-0.37.0-checks.md`).

T21's one failure is the RIVA's known mip floor: levels below 8x8 are
not drawn (T21Ok 7, the 4x4 quad). It is the same on both drivers.

## Not known

- Which build was installed at boot 39. It was a RIVA development
  build, so whether the released 0.36.0 nvidia package already draws
  nothing in Glide is not known.
- Why the candidate refuses or drops Glide's draws on this engine.

## Next

Run the 0.36.0 nvidia package's files on this card. If Glide draws
there, bisect `0.36.0..55a661a2` for the nvidia engine's
render-interface path (`v9x_r3d_fallback`, the NV3 map's refusals).
