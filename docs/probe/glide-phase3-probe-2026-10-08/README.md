# Glide Phase 3: V9XGLIDP's texture, blend and fog checks, 2026-10-08

- Machines: MICHAEL-NETBOOK (GMA 950, Gen3), boot 127, agent 10.0.1.248;
  `Win98SE-Fast-D3D` (86Box, software engine), boot 602, port 9878.
- Record: [Glide Phase 3](../../decisions/2026-10-08-glide-phase3-textures-fog.md).
- Plan: [glide-2x-wrapper.md](../../plans/glide-2x-wrapper.md), Phase 3.

| File | Boot | What it is |
|---|---|---|
| `NB-B127-V9XGLIDP.INI`, `NB-B127-V9XGLIDE.LOG` | 127 | Gen3: 21 of 21, five texture surfaces uploaded, no fog dropped |
| `SOFT-B602-V9XGLIDP.INI`, `SOFT-B602-V9XGLIDE.LOG` | 602 | Software engine: 21 of 21, CPU textures, no fog dropped |

The first fourteen checks are Phase 2's. The DLL and probe are build
d36e4b9-dirty: the tree that became the Phase 3 commit.
