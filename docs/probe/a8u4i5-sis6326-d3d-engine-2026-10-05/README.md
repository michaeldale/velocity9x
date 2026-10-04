# A8U4I5: SiS 6326 Direct3D engine under V9XDDP, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), desktop 800x600x16.
- V9XDDP switches to exclusive 640x480x16. It draws into a 64x64 RGB565
  target at 12C000h (pitch 128); its first textured texture follows at
  12E000h.
- Record: [2026-10-05 SiS 6326 Direct3D engine](../../decisions/2026-10-05-sis6326-d3d-engine.md).
  Issue: [the hard locks](../../issues/2026-10-05-a8u4i5-hard-lock-under-sis-d3d-v9xddp.md).

| Boot | Build | Files | What happened |
|---|---|---|---|
| 212 | `1feb92a-dirty` | `B212-*` | First run. Every textured draw refused (`wrap_either`), DESTCOLOR drawn. Froze at the depth fill; Michael reset it. |
| 215 | `d3dc21f` | `B215-*` | Froze at the same depth fill; Michael reset it. `V9XTRACE.INI`: 89 2D idle timeouts (`S2ID`), Lock answering WASSTILLDRAWING. |
| 216 | probe `p4a`, `p4b` | `B216-SIS3D-P4*` | SIS3D `/phase4`: V9XDDP's base, tiled and negative textured draws replayed through the driver's mapping, alone: all idle, all green. 82A8h never busy. |
| 217 | `d3dc21f-dirty` (quarantine) | `B217-*` | The 3D idle timeout recorded as the first fault (`S3ID`, 89FCh 00200074h); 2D stopped with it; V9XDDP completed, no freeze. |
| 218 | + `V9XSIS3D.TXT` | `B218-*` | The stalled batch's register stream: batch 27, the first textured one, after untextured batch 26; stall at the wait after the triangle. |
| 219-227 | probe `p4c`-`p4f` | `B2xx-SIS3D-P4B-*` | `/phase4b` replays of that stream (below). |
| 228 | `d3dc21f-dirty` (Cpix) | `B228-*` | Untextured draws drawn textured with Cpix: V9XDDP complete, 0 idle timeouts (`B228-V9XSNAP.INI`). |

`/phase4b` variants, one per boot. Each is the logged batch 27 at 3 MiB,
preceded by:

| File | Preceded by | Result |
|---|---|---|
| `B219-...-LAYOUTS` | batch 26 (blend), texture far from target | stalled |
| `B220-...-VA` | an untextured batch without blend | stalled |
| `B221-...-VB` | batch 26; batch 27's texture words before its enable | stalled |
| `B222-...-VC` | batch 26, then 8A00h = 800h, ONE/ZERO and an idle wait | stalled |
| `B223-...-VD` | nothing | idle |
| `B224-...-VE` | an untextured batch with the cache bits (80A0h) on | stalled |
| `B225-...-VF` | an untextured batch; batch 27 without large cache and bit 15 | stalled |
| `B226-...-VG` | the "untextured" batch drawn textured, colour mode Cpix | idle |
| `B227-...-VI` | an untextured batch with perspective on | stalled |

    python ddp_compare.py ../a8u4i5-sis6326-registers-2026-10-04/V9XDD-CARD2-SIS228-B207.INI B228-V9XDD.INI
