# Mystique: 3DMark 2000 at 640x480 leaves large areas of Adventure black, with 911 textures refused for placement

Date: 2026-11-27. Machine: A8U4I5, Matrox Mystique (MGA-1064SG,
`102B:051A`, revision 02, 4 MiB), the 0.61.0 tree's `matrox` set.
Status: open, cause not confirmed. The first 3DMark 2000 run on the
card.

## What is seen

3DMark 2000 by hand at 640x480x16 (16-bit Z, triple buffered, 16-bit
textures, 2400 KB of the 4096 KB for buffers by 3DMark's count) runs
to its score, 411, with no engine timeout. Live screenshots every 30
seconds (`docs/probe/mystique-1.0-2026-11-27/mystique-3dmark-640/3dmark2000-live-screenshots.jpg`):

- **Adventure:** in several frames large areas of ground and sky are
  black, with buildings and a few props drawn over them.
- **Helicopter:** smoke and explosions draw as white boxes. This part
  is the Matrox engine's known limit: the Millennium II drew 3DMark
  2000's particles as untextured white quads in October (no blending,
  no 4444;
  [record](../decisions/2026-10-20-mga2164w-0.21.0.md)).

Counters across the run (`snapshot-before.ini`, `snapshot-after.ini`):
95,999 primitive calls, 10,132 batches refused by the engine,
`MgaSetupRefusedSplit=45120`, `MgaTexRefusedPlace=911` (last refused:
a 256x128 16-bit texture, pitch 512), 2,651 flips, nothing drawn into
the presented buffer, no idle timeout or reset.

The likeliest reading is that 4 MiB less three 640x480 buffers and a
Z buffer leaves too little for Adventure's textures, so the HAL refuses
to place them and those draws go to the software fallback or are
dropped; the Millennium II, with 8 MiB at 1024x768, did not show black
areas. Not checked: whether a refused placement falls back or drops
the draw, and what `MgaSetupRefusedSplit` counts.

## To find out

Run Adventure with double buffering (one 640x480 buffer back) and see
whether the black areas and `MgaTexRefusedPlace` go. Read where a
placement refusal goes in the `matrox` HAL. Record:
[decision](../decisions/2026-11-27-1.0-mystique-a8u4i5.md).
