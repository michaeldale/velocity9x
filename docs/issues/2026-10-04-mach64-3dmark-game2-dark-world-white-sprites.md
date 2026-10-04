# Mach64: 3DMark 99 Game 2 draws its world mostly black, with white squares for sprites

Date: 2026-10-04. Machine: A8U4I5 with the ATI 3D Rage XL PCI
(`1002:4752`), Mach64 engine path. Status: fixed the same day. The
D3D core's 256-entry texture handle table filled, so lightmaps were
drawn untextured. None of the candidates below was the cause. See
`docs/decisions/2026-10-04-texture-handle-table-full.md`. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/flip/video-game2-every-1s.png`,
frames from Michael's recording of the monitor
(`2026-10-04 10-56-00.mkv`, 28-44 s).

## Symptom

In Game 2 (First Person) most of the room is black. Floor plates, rails,
some walls and the gun draw. Bright white squares stand where sprites or
particles belong. Game 1 (Race) in the same run draws correctly.

## What is known

- The engine refused nothing over the run (`M64Refused=0`), so these
  draws reach the chip, and are drawn wrong or drawn black.
- Michael believes it predates the flip change made the same morning
  (`docs/decisions/2026-10-04-rage-xl-page-flips.md`). No earlier picture
  of Game 2 on a Mach64 is on file to confirm it; the Gateway's runs
  were never watched.

## Candidates, none tested

- A lightmap or multi-pass blend (DESTCOLOR / SRCCOLOR) drawn with the
  wrong factor, leaving the base pass dark.
- Colour-keyed or alpha sprites drawn without their transparency: the
  white boxes. Colour keys are refused by policy elsewhere, but none
  were refused in this run, so these sprites take some other path.
- A texture format or texture-op mapping that reads black.

## Next

Capture the batches Game 2 sends (state, blend, texture format and op)
for a census like Half-Life's. Then reproduce the darkest one in a
V9XDDP scene, and compare with ATI's own driver on the same card if it
can be installed.
