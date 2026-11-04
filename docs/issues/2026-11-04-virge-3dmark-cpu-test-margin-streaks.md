# ViRGE/DX: 3DMark 2000's CPU Speed test leaves streaks of stale video memory in the frame's margins

Date: 2026-11-04. Status: open, parked (minor; Michael: "it isn't major
issue").

Machine: A8U4I5 (Pentium III, Windows 98), PCI S3 ViRGE/DX `5333:8A01`,
4 MiB, `s3` family. Seen on the released 0.35.0 build (boot 661) and on
`69bb160c-dirty` with the mip filter fix (boot 662). Context:
[`../decisions/2026-11-04-virge-dx-silicon-3d-and-mip-filter-swap.md`](../decisions/2026-11-04-virge-dx-silicon-3d-and-mip-filter-swap.md).

## What is seen

3DMark 2000, custom benchmark at 800x600x16, 16-bit Z, triple buffered.
The CPU Speed test draws a 640x480 view centred in the 800x600 frame. The
margins around it carry horizontal streaks that look like stale video
memory, not black. Captured by OBS from the card's VGA output:
`../probe/virge-dx-a8u4i5-silicon-2026-11-04/3dmark-before-fix/obs-cpu-test-margins.jpg`.

Michael reports that other cards show black there. Not checked on any
other card by capture, and not checked whether the streaks move between
frames.

## Not known

- Whether 3DMark clears the whole back buffer each frame or only its
  viewport. If only the viewport, the margins hold whatever the flip chain's
  pages held before, and the driver's part is what those pages contained
  when they were made (they are not cleared at creation).
- Whether the streaks are in every page of the chain or one.

## Next, when picked up

Grab the three pages with `V9XTRACE -grab` during the CPU test and compare
them with the same grab on a card that shows black. If the margins differ
only by what the pages held at creation, decide whether surface creation
should clear flip-chain pages, and measure the cost.
