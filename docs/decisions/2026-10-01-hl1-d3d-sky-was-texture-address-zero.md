# Half-Life's Direct3D sky on the Mach64 was texture address 0, refused and drawn by nobody

Date: 2026-10-01
Machine: Gateway SOLO2150, ATI Rage Mobility-M, boots 84-86, full ATI set
(ABI 2026100104, then 2026100105; the fix's HAL alone on boot 86), every
installed file hash-verified.
Evidence: [`../probe/hl1-d3d-sky-address-2026-10-01/`](../probe/hl1-d3d-sky-address-2026-10-01/)

## The defect

The operator saw Half-Life's sky corrupted in Direct3D and not in OpenGL
(`gateway-hl-d3d-sky-before.jpg`: the ceiling holds smeared old frames).
The last Direct3D session's snapshot had 3,183 Mach64 refusals on texture
address (`M64Policy11`), and the core answers a refused batch with DD_OK and
draws nothing (`d3d_core.c`, `batches_engine_refused`). Half-Life does not
clear the colour buffer, so an undrawn sky shows whatever was there. The
OpenGL ICD draws a refused batch on the CPU instead, which is why GL was
unaffected.

## Measured

Two appended diagnostics: what an address refusal was asked for
(`M64AddressRefused`, `M64AddressRefusedSize`; ABI 2026100104), and the
values set for each texture-address render state (`AddressSeen`, `U`, `V`;
ABI 2026100105).

| `mwd5`, three timedemos | `gwref2` (before) | `gwaddr` (fix) |
|---|---|---|
| `AddressSeen` (TEXTUREADDRESS) | 0x2: 1 only | 0x2 |
| `AddressSeenU` / `V` | **0x3: 0 and 1** | 0x3 |
| address refusals | 3,183, all address 0, last 256x256 | **0** |
| alpha-test refusals | 13,481 | 13,065 |
| shape refusals | 357 | 357 |
| engine resets | 0 | 0 |
| best timedemo | 13.44 fps | 12.09 fps |

Half-Life sets `TEXTUREADDRESSU`/`V` to 0, a value d3dtypes.h does not
define, on textures of the sky's size. The netbook's Gen3, which also
refuses an undefined address, refused nothing in the same demo, so the 0 is
something this game sends to this driver's Mach64 caps; which cap was not
established (the Mach64 does not claim MIRROR; that is a guess).

## The change

`v9x_d3d_state_fill` takes an address value outside WRAP..BORDER as CLAMP,
what the same game's OpenGL renderer asks for its sky. It is the shared
state translation, so every engine gets it; the four defined values pass
unchanged. `tests/host/test_d3d_state.c` gains the case, watched failing
first; the routing test now carries MIRROR rather than an out-of-range
marker.

The operator confirmed on boot 86 that the sky is right and alpha-tested
surfaces look correct. The frame rate fell by about a tenth, which is the
sky now being drawn.

## Not established

- Why Half-Life sends 0 here and not to the Gen3.
- What the 13,065 alpha-test refusals are; they are dropped the same way,
  and the operator saw nothing missing.
- Any other application sending an undefined address.
