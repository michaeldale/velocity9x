# The G45 and Q45 run 0.28.0: issue 3's reporter's field report

Date: 2026-11-04 (the report is from 2026-10-24)
Machines: the issue 3 reporter's Dell Inspiron 560 (G45, GMA X4500,
`8086:2E22`) and HP Compaq 8000 Elite SFF (Q45, GMA 4500, `8086:2E12`), the
two machines whose DOS surveys the blind build was made from
([`2026-10-23-g4x-gma4500-from-surveys.md`](2026-10-23-g4x-gma4500-from-surveys.md)).
Evidence: the reporter's comment on GitHub issue 3, 2026-10-24. No
diagnostic files came with it; nothing here was measured by us.

## What the report says

On release 0.28.0 (the first build to claim either chip), both machines
work. The reporter "briefly tested OpenGL, Direct3d and Glide" and ran
3DMark 2000's default benchmark:

| Machine | 3DMark 2000, default |
|---|---|
| Dell Inspiron 560 (G45) | 1942 |
| HP Compaq 8000 Elite SFF (Q45) | 1760 |

## What this settles, and what it does not

- The blind build's three open guesses held on G4x silicon: the GTT at
  BAR0 + 2 MiB, PIPELINE_SELECT `69040000`, and the 6-dword
  3DSTATE_DEPTH_BUFFER. A wrong GTT offset or a refused first 3D batch
  would not have reached a 3DMark score.
- It is one report, from 0.28.0. Nobody has run a later release on either
  machine, no V9XDIAG files or probe outputs exist, and which Direct3D,
  OpenGL and Glide titles were run is not known.
- The GM45 (`8086:2A42`) shares the code but is a different part; this
  report says nothing about it
  ([`2026-11-04-gm45-gl40-from-a-survey.md`](2026-11-04-gm45-gl40-from-a-survey.md)).
