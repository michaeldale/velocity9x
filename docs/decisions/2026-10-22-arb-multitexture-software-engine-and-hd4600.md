# GL_ARB_multitexture on the software engine and the HD 4600: ARB draws what SGIS draws, bit for bit, and Quake 3's demo001 runs 52.8 fps against 41.4

Date: 2026-10-22. Plan:
[`../plans/opengl-arb-multitexture.md`](../plans/opengl-arb-multitexture.md),
the rest of step 4 (the software engine) and step 5 on the HD 4600.
Follows [2026-10-22-netbook-arb-multitexture-quake3.md](2026-10-22-netbook-arb-multitexture-quake3.md).
Evidence:
[`../probe/arb-multitexture-soft-and-hsw-2026-10-22/`](../probe/arb-multitexture-soft-and-hsw-2026-10-22/).

## Software engine: 86Box `Win98SE-Fast-D3D`

Port 9878, vbe package, software Direct3D, 1024x768. The guest's set
dated from 2026-10-05, before render ABI 6. The whole vbe set from this
tree at `8aee07c0` (`run-checks` green) went in by one WININIT rename at
boot 617, with plain `dest=src` lines and no `NUL=` lines
(`9878-soft-boot617-WININIT.INI`): DRV, VxD, HAL, settings page, ICD.
The same rename updated the guest's agent from 0.9.1 to 0.12.1. After the
reboot (boot 618) every installed file's size matched the package. The
previous set is in the session scratchpad.

V9XGLP (`9878-soft-boot618-V9XGLP.INI`): `Result=PASS`, 52 Oks, none 0.
Every ARB case reads exactly the software engine's SGIS colour, in
immediate mode, `glDrawArrays` and `glDrawElements`: MODULATE `0x636508`,
REPLACE `0x84FF42`, BLEND `0x630063`, DECAL `0xA5B639`, unit 1 left
`0x000000`, right and disabled `0xC66531`. Those SGIS values are the ones
recorded on this engine on 2026-10-05
([2026-10-05-sgis-multitexture-software-engine.md](2026-10-05-sgis-multitexture-software-engine.md)).
This was the check step 4 named.

## HD 4600 (W9QI5A83, boot 63, at 10.0.1.254)

The rest of the driver stayed at the dev set it ran (`9d4c7012-dirty`,
ABI 6). Only `V9XGL.DLL` was replaced by `put`, with Quake 3 closed. It
went from `9f62e6f5-dirty` to `8aee07c0` (CRC32 AA6F4E77). The old ICD
is in the session scratchpad.

V9XGLP (`hsw-boot63-V9XGLP.INI`): `Result=PASS`, 52 Oks, none 0. ARB
matches this chip's SGIS colours exactly, all three ways. MODULATE reads
`0x636510` here, against `0x636508` on the software engine and
`0x6B6510` on the GMA 950. All are within the case's tolerance.

Quake 3 demo 1.11, `demo001` under `timedemo 1`, full screen 640x480,
16-bit colour and Z (`+set r_fullscreen 1`; the config had been left
windowed by an earlier session and was restored after). Three processes,
one after another:

| ICD | `r_ext_multitexture` | Quake 3 reports | Result |
|---|---|---|---|
| `9f62e6f5-dirty` | 1 | "GL_ARB_multitexture not found", "multitexture: disabled" | 1346 frames, 32.5 s: 41.4 fps |
| `8aee07c0` | 1 | "multitexture: enabled" | 1346 frames, 25.5 s: **52.8 fps** |
| `8aee07c0` | 0 | "multitexture: disabled" | 1346 frames, 32.5 s: 41.4 fps |

The multitexture run drew 48,763 two-unit batches (1,223,841
triangles), the same count as the netbook's run of the same demo. None
were split or refused, and `cpu=0/0`. The two runs without multitexture
drew none.

## Not established

- Screenshots of issue 11's views were not taken. Michael reports the
  issue fixed in 0.24.0 and Quake 3 and Half-Life looking right on the
  ARB ICD; nothing here checked the frames.
- One run per case: the spread between runs was not measured. The two
  runs without multitexture read exactly the same, 41.4 fps.
- Quake 2, and Half-Life on A8U4I5, remain from step 5.
