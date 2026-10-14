# Netbook GMA 950: the 3DCIF demos on Gen3, 2026-10-14

Plan: [ati-3dcif-wrapper.md](../../plans/ati-3dcif-wrapper.md), Phase 3,
second engine.

- **Machine:** MICHAEL-NETBOOK, GMA 950 (945GSE), 1024x576. Boot 180,
  `ColourLayout=565-auto`: its VBIOS has no 15 bpp modes.
- **Driver:** the `intel-gma` package of `ed8f18a5`. The render interface
  takes only an exact ABI, so every client went together:
  - by WININIT rename: the driver, mini-VDD, HAL, settings page and
    OpenGL ICD;
  - by direct copy: GLIDE2X.DLL in SYSTEM and in the NFS II SE and
    Carmageddon II folders, and GLIDE3X.DLL in SYSTEM.

  The agent's `reset` verb restarted the machine.
- **Demos:** the six SDK folders, pushed to `C:\3DCIF`, with
  `WIN.INI [AtiDemo] TexAddrTiling=FALSE`. `build\cif\ATI3DCIF.DLL` was
  copied beside each executable.
- **The runs:** each demo ran about 40 s, then got Escape, and every one
  exited on it.
- **On the screen:** Michael watched the demos afterwards: "it is
  working", with some flicker, and some demos running too fast
  ([issue](../../issues/2026-10-14-3dcif-gen3-flicker-and-speed.md)).

`demo-summaries-before-shrink.txt` is the first pass:

| Demo | Frames | Drawn | Refused | Textured draws skipped |
|---|---|---|---|---|
| Cubes | 23,475 | 231,802 | 0 | 0 |
| ATi-3D | 11,413 | 114,175 | 0 | 0 |
| Pixie | 2,284 | 1,233,193 | 0 | 0 |
| Fairy | 2,325 | 1,241,165 | 0 | 0 |
| Laputa | 7,390 | 1,097,910 | 0 | 0 |
| Knight DEMO | 680 | 708,497 | 0 | 42,119 |
| Knight MORPH | 1,182 | 742,320 | 0 | 0 |
| Knight VQ | 337 | 711,092 | 0 | 205,772 |

- **The blended sprites draw on Gen3.** Pixie, Fairy and Laputa refuse
  none of them; both ATI engines refuse two draws a frame.
- **Knight DEMO:** its 512x512 4444 texture found no room
  (`hwtex create 02000200 format=4 hr=8876017C`, out of video memory).
- **Knight VQ:** its 1024x256 RGB565 texture was refused with
  `DDERR_INVALIDPARAMS` (`80070057`), though Gen3 describes 1024
  ([issue](../../issues/2026-10-14-gen3-refuses-1024x256-texture.md)).
- **The 5:6:5 log line** fired once a run, as intended.

`knight-summaries-after-shrink.txt` and `knight-shrink-lines.txt` are the
two Knight demos again, with ATI3DCIF.DLL rebuilding a texture a level
smaller when no surface can be made for it:

| Demo | Frames | Drawn | Refused | Textured draws skipped | Textures made smaller |
|---|---|---|---|---|---|
| Knight DEMO | 744 | 813,469 | 0 | 0 | 512x512 to 256x256 |
| Knight VQ | 591 | 1,246,189 | 0 | 0 | 1024x256 to 512x128; 512x512 to 64x64 |

The netbook's heap holds about 5.7 MB beside a triple-buffered
1024x576 chain. A 512x512 VQ texture had to go three levels down.
