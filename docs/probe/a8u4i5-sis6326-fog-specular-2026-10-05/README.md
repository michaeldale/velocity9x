# A8U4I5: SiS 6326 vertex fog and specular, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), desktop 800x600x16.
- Record: [2026-10-05 SiS 6326 fog and specular](../../decisions/2026-10-05-sis6326-fog-specular.md).

| File | Boot | What it is |
|---|---|---|
| `B230-SIS3D-P5.TXT` | 230 | SIS3D `/phase5` (build `sis3d-20261005-p5a`): nine draws through the driver's mapping, each pixel against Direct3D's formula (`*Pixel`, `*Expected`, `*Result`) |
| `B231-V9XDD.INI`, `B231-V9XDDT.TXT` | 231 | V9XDDP with fog and specular published (working tree on `4128908`) |
| `B231-V9XSNAP.INI` | 231 | V9XTRACE after the run: `EngineIdleTimeouts=0`, `BatchesEngineRefused=5`, `FlipHandled=23` |

    python ../a8u4i5-sis6326-d3d-engine-2026-10-05/ddp_compare.py ../a8u4i5-sis6326-registers-2026-10-04/V9XDD-CARD2-SIS228-B207.INI B231-V9XDD.INI
