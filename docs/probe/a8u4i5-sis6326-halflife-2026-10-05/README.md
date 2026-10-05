# A8U4I5: Half-Life on the SiS 6326, Direct3D and OpenGL, 2026-10-05

- Machine: A8U4I5, P3 1 GHz, SiS 6326 card 2 (rev 0Bh), Velocity9x build
  `0eb3030`. Half-Life 1.1.1.0 (`C:\SIERRA\Half-Life`), `timedemo mwd5`
  (393 frames, a Xen level), 640x480 (`-w 640 -h 480`), fullscreen,
  `-console -condebug`.
- Plan: [sis-6326-hardware-3d.md](../../plans/sis-6326-hardware-3d.md),
  phase 5.

## Results

| Renderer | Engine | Run 1 | Run 2 | Run 3 |
|---|---|---|---|---|
| Direct3D (`-d3d`), boot 291 | SiS 6326 hardware | 10.358 fps (37.943 s) | 11.416 fps (34.426 s) | 11.309 fps (34.751 s) |
| OpenGL (`-gl`), boot 292 | Velocity9x ICD on the CPU engine (`[Velocity9x] Direct3D=2` for that boot) | 0.803 fps (489.115 s) | 0.811 fps (484.654 s) | 0.811 fps (484.648 s) |

Scores are recorded, not compared.

**OpenGL is not on the SiS engine.** The engine refuses every
render-interface ("explicit") draw. The render interface has a software
fallback only for Intel Gen3, the one engine measured to agree with the CPU
on colour and depth encodings. So with the hardware engine selected, the
ICD would draw nothing. For the OpenGL runs only, SYSTEM.INI asked for the
software engine; SYSTEM.INI and Half-Life's `config.cfg` were restored
afterwards (boot 293).

## Files

| File | What it is |
|---|---|
| `B291-QCONSOLE-D3D.LOG`, `B292-QCONSOLE-GL.LOG` | Half-Life's console log for each renderer (it rewrites the file at each start) |
| `B291-HL-D3D-RUN2.png`, `-RUN3.png` | Agent screenshots during the Direct3D timedemo |
| `B292-HL-GL-RUN1.png` | The same during the OpenGL timedemo |
| `B291-V9XSNAP-AFTER-HL-D3D.INI` | V9XTRACE after the Direct3D runs, cumulative for the boot (Final Reality, V9XDDP and 3DMark 99 ran before) |
| `B292-V9XSNAP-AFTER-HL-GL.INI` | V9XTRACE after the OpenGL runs |

## Direct3D: what was drawn

No idle timeout and no declined flip. The SiS mapping refused 37,303
batches, all for reason 4, the blend, less the three V9XDDP refused
earlier in the boot. The last refused pair was DESTCOLOR/SRCCOLOR, the 2x
modulate, which the mapping refuses as unmeasured. Those draws are missing
from the frames, so the Direct3D numbers are for an incomplete picture.
The screenshots show textured, lightmapped geometry, the view model and
the HUD.
