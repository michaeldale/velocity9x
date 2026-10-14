# The GMA 950 draws a colour-keyed texture's key colour

Date: 2026-10-15

Status: Open, not new. Found by V9XDDP's new `ColorKeyHalves` check on the
netbook (GMA 950, boots 181-182). Evidence:
`docs/probe/table-fog-other-engines-2026-10-15/nb-*`.

The check draws a texture over blue. Its left half is the source key
(magenta) and its right half green.

| Texture | Left | Right |
|---|---|---|
| RGB565 | `F81F`, key drawn | `07E0` |
| ARGB1555 | `F81F` | `07E0` |

The key is not honoured: the left half should keep the blue destination.
The old `ColorKeyOk` reads 0 as well, before and after the 2026-10-14
build, so this predates it.

The Rage XL now keys in hardware
([record](../decisions/2026-10-14-rage-xl-texture-colour-key.md)). Gen3's
fragment program has a texture kill, and the ViRGE rewrites keyed texels
to alpha 0. Either would serve here; not yet chosen.
