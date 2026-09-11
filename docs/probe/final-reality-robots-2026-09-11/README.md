# Final Reality Robots, our HAL against Microsoft's software rasterizer

Evidence behind
[every Final Reality texture is refused for having no pixel format](../../issues/2026-09-11-every-final-reality-texture-is-refused-for-having-no-pixel-format.md).
All from `Win98SE-Trio64`, 86Box, agent port 9871, boot 349, `Direct3D=2` so
every Direct3D draw is served by the CPU rasterizer. The Robots scene was run
once through each rendering platform, one after the other in the same boot,
with every other test cleared and the five-times repeat off.

| File | What it holds |
|---|---|
| `robots-valley-hal-vs-software.png` | before the fix, matched pair, same camera position: ours untextured, the reference textured |
| `robots-closeup-hal-vs-software.png` | before the fix, the robot itself, the same story at close range |
| `fr-advanced-options-hal.png` | the test selection and the capabilities FR reads from our device |
| `fr-advanced-options-software.png` | the same page with Microsoft's device selected |
| `v9xsnap-after-both-runs.ini` | `V9XTRACE` after both runs: `D3dTextureRefusedFormat=39793`, `D3dTextureRefusedLast=0xFFFFFFFF` |
| `robots-valley-before-vs-after.png` | this driver either side of the fix, same camera position |
| `robots-valley-fixed-vs-software.png` | after the fix, against the reference - the question this run was asked |
| `robots-closeup-fixed-vs-software.png` | after the fix, the robot |
| `v9xsnap-after-fix.ini` | boot 351: 466 textures, 8753 primitive calls, every refusal counter zero |
| `trio64-probe-after-fix.ini` | the probe report from the same boot, carrying the new `DisplayFmtTex*` readings |

The before files are boot 349 and the after files boot 351, the reboot being
what installed the fixed `V9XHAL.DLL`. The fix is
[a texture with no pixel format is in the display's format](../../decisions/2026-09-11-a-texture-with-no-pixel-format-is-in-the-displays-format.md).

The two options pages are worth reading side by side. Our device offers
alpha blending, multiplicative alpha and subpixel accuracy, which Microsoft's
does not; Microsoft's offers mip-mapping and depth fog, which ours does not.
Neither list is the reason the pictures differ - the textures are.

`Multiplicative alpha (darken)` is lit on our device for the first time here,
which is the `DESTCOLOR` cap added earlier the same day
([record](../../decisions/2026-09-11-the-lightmap-pass-now-draws.md)). FR
reads it and would use it; nothing in this run shows whether it did.

## Capturing

The guest agent's `screenshot` reads the GDI primary and returns black while
DirectDraw is flipping, so a fullscreen benchmark cannot be captured that
way. These came from the host with `PrintWindow(hwnd, hdc, 2)` on the 86Box
window, cropped to the guest surface at (3, 72). Pick that window by title -
more than one 86Box process runs on this host.
