# A8U4I5: SiS 6326 colour blend factors, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), desktop 800x600x16.
- Record: [colour blend factors](../../decisions/2026-10-05-sis6326-colour-blend-factors.md).

| File | Boot | What it is |
|---|---|---|
| `B293-SIS3D-P7.TXT` | 293 | SIS3D `/phase7` (build `sis3d-20261005-p7a`): twelve blends through the driver's mapping with the pair forced, each pixel against Direct3D's formula (`*Fill`, `*Pixel`, `*Expected`, `*Result`) |
| `B294-V9XDD.INI`, `B294-V9XDDT.TXT` | 294 | V9XDDP on the driver taking the colour factors |
| `B294-V9XSNAP-AFTER-DDP.INI` | 294 | V9XTRACE after V9XDDP: 2 refusals (colour key, texture format), none for blending |
| `B294-QCONSOLE-D3D.LOG` | 294 | Half-Life `timedemo mwd5`, Direct3D, 640x480, three runs |
| `B294-HL-D3D-RUN2.png`, `-RUN3.png` | 294 | Agent screenshots during the timedemo |
| `B294-V9XSNAP-AFTER-HL-D3D.INI` | 294 | V9XTRACE after Half-Life: no idle timeout, no declined flip, no new refusal |

    python ../a8u4i5-sis6326-d3d-engine-2026-10-05/ddp_compare.py ../a8u4i5-sis6326-fr-stall-2026-10-05/fold-b291/B291-V9XDD.INI B294-V9XDD.INI
