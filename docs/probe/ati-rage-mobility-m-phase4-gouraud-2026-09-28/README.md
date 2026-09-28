# ATI Rage Mobility-M Phase 4 Gouraud triangle

Target: Gateway Solo 2150, ATI Mobility-M `1002:4C4D` revision `64`, subsystem
`107B:2150`, 4 MiB SGRAM, 1024x768x16 desktop, agent boot counter 9.

Build `ati-phase4-gouraud-20260928-a` reused the guarded Phase 3 off-screen
surface at VRAM offset `0x00200100`. It changed only `SETUP_CNTL` from flat
vertex-3 shading (`0x18`) to Gouraud shading (`0x00`) and supplied red, green
and blue ARGB values to vertices 1, 2 and 3. Four interior assertions require
red-, green- and blue-dominant pixels plus one mixed-color pixel.

Two accepted same-boot runs passed and produced byte-identical artifacts:

- `ATI4G0.TXT` and `ATI4G0-PASS2.TXT`: CRC32 `BBA3A850`, SHA-256
  `CFA324CCC0B0F294E5B3E693A1009DFA9406D26374BA967E8F7960F5B861C7A4`;
- `ATI4G0.BMP` and `ATI4G0-PASS2.BMP`: CRC32 `C1C5460C`, SHA-256
  `0CA6A2A36C9BE21DB8118C8EADBC09362C04AE605679A96412D565CEAD678830`;
- 256 changed pixels, bounding box `(8,6)` through `(38,21)`;
- zero interior, exterior, physical-guard, persistent-state, and VRAM-restore
  mismatches;
- zero timeouts and zero recovery resets;
- `GUI_STAT` idle before and after (`0x00800000`).

The first invocation is retained as `ATI4G0-REVIEW-A.*`. It refused during
PCI preflight, before MMIO mapping or any engine/framebuffer write, because
the PCI command low byte was `0x80` rather than the normal `0x87`. A mouse
movement woke the desktop/power-management state; the unchanged payload then
produced both accepted passes. This is environmental refusal evidence, not a
failed Gouraud render.

Public ATI acceleration remains disabled. Later Phase 4 features and the
publication gates are still open.
