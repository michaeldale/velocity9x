# HD 4600, Glide and 3DCIF: Glide passes; 3DCIF draws only without depth, and Laputa hangs

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boots 26 and 27,
HAL `13d11e47` (mipmaps). Plan steps 5 and 6 of
[intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md),
probes only, no code changed. Evidence:
[`../probe/hsw-4570-glide-3dcif-2026-10-18/`](../probe/hsw-4570-glide-3dcif-2026-10-18/).

## Glide

- **V9XGLIDP**, with the tree's `GLIDE2X.DLL` beside it in `C:\V9XGMA`:
  - `Result=PASS failures=0`, all 21 checks. They cover clears and flips,
    an untextured triangle, the W-buffer reject and accept, the clip
    window, a line, the snap bias, 565 and palette textures with a
    palette change, chroma key on and off, half blend, and table fog.
  - The DLL bound engine 9 (`describe ... engine=9`). It drew 29 draws
    and refused none.
  - Haswell has no software fallback, so a refused draw would have been
    counted. The 29 went to the engine.
- **V9XGL3P**, against `SYSTEM\GLIDE3X.DLL`: `Result=SESSION Step=done`.
  Open, clear, swap and close ran with no Glide error.
- **No title** was run: none is installed on the 4570.

## 3DCIF: the eight SDK demos

Setup as on the netbook and A8U4I5:
- the six SDK folders in `C:\3DCIF`, with `build\cif\ATI3DCIF.DLL`
  beside each executable;
- `WIN.INI [AtiDemo] TexAddrTiling=FALSE`;
- each demo about 40 s, then Escape (`rundemos.ps1`).

| Demo | Frames | Drawn | Refused | On screen |
|---|---|---|---|---|
| Cubes | 2,567 | 25,308 | 0 | textured cubes, correct |
| ATi-3D | 2,557 | 0 | 10,228 | black |
| Pixie | 2,543 | 619,120 | 757,758 | the floor; the depth-tested pillars missing |
| Fairy | 2,577 | 631,420 | 783,400 | partial |
| Laputa | 1 | 0 | all | black; **hangs** |
| Knight DEMO | 958 | 11,885 | 1,105,887 | (boot 27) |
| Knight MORPH | 1,266 | 15,192 | 757,172 | (boot 27) |
| Knight VQ | 1,264 | 30,336 | 2,613,850 | (boot 27) |

**Every refusal is raw depth.**
- The DLL details the first 16 refusals of each process; 112 are in
  the log. Every one is `result=4` with a raw depth address: `E5A10000`
  or `E597A000`. No detailed refusal is without depth.
- This is the gap the plan named for step 6. A raw-depth draw carries a
  placeholder surface with no `lpGbl`, so `v9x_d3d_hsw_depth_surface`
  refuses it.
- The HAL's own refusal counters stay at 0. The refusal comes from
  `accepts`, which does not count.
- The demos' Z buffers are this HAL's: `ZPlaced=4`, `ZPlacedPitch=1280`.
  That is `create_surface`'s tiled placement for a 640-wide D16 buffer.
  So the plan's first option fits: accept a raw depth whose address and
  pitch match a Z surface this HAL placed.

**Laputa hangs after its first frame, on both boots.**
- It runs as two processes. The first calls Init, GetInfo and Term, then
  exits. The second registers its textures and draws frame 1, every draw
  of it refused. Then it stops.
- Its window stays up, and Alt+F4 does nothing.
- Two HAL snapshots taken 5 s apart while it hangs differ only in their
  timestamps. It makes no flip, lock or draw call, so it is spinning or
  blocked in its own code.
- The engine is not hung: every timeout counter is 0, and the Knight
  demos ran after the reset.
- On the GMA 950 and the Rage XL, where its draws succeed, Laputa ran.
  So the hang is most likely its reaction to every draw failing. That is
  not established; the raw-depth fix will say.
- In the first pass, the hung Laputa held the display, so the three
  Knight runs after it never drew. They were rerun on boot 27 after a
  `reset`.
- Laputa was left hung on boot 27.

## What follows

- **Glide:** nothing more on Haswell before a title.
- **3DCIF:** a raw-depth lookup against the Z buffers `create_surface`
  placed is the next change. Then the eight demos again, Laputa first.
