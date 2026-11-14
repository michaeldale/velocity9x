# GT218: a tiled-kind alias window works, measured by a 2D round trip

Date: 2026-11-14

Steps 1 and 2 of [docs/plans/nvidia-gt218-3d-z.md](../plans/nvidia-gt218-3d-z.md),
on M-BXMFA8 (GeForce 210, GT218 A8), boot 117. Evidence:
`build/driver-results/m-bxmfa8-gt218-3d/z1-V9XN5CH.INI` and
`z2-V9XN5CH.INI`.

## Step 1: the windows break nothing

b013f697 added PDEs 1 to 3 to the channel's page directory. Each maps
the same 512 MiB of video memory as PDE 0, through its own page table
with one storage type in every PTE (kind 70h at VM 512 MiB, 6Ch at
1 GiB, 28h at 1.5 GiB, compression off). It also widened the VM DMA
object's limit from 512 MiB to 2 GiB and grew the reserve to 2 MiB.
With that channel, `V9XN5CH /lean /3d /3d:draw /3d:gouraud /3d:texture`
(`z1`) passed every stage it has: FIFO, PGRAPH, 2D fill, copy and three
depths, 3D setup, solid, Gouraud and textured draws at 32 and 16 bpp.

## Step 2: through the kind-70h window and back

`V9XN5CH /lean /z:tiled2d` (`z2`, bd4cddce). One push, 88 words, two 502D
copies:

1. a 128x72 X8R8G8B8 gradient in a linear surface A (window + 3.5 MiB)
   to a block-linear surface T (window + 3.625 MiB, tile mode 10h, eight
   rows a block), T addressed at `V9X_NV50_VM_TILED +` its video memory
   address;
2. T to a linear surface B (window + 3.75 MiB), prefilled with the
   pattern.

Each surface was programmed as Mesa's `nv50_2d_texture_set` does
(block-linear: format, layout 0, tile mode, depth 1, layer 0, then
width, height, address at the format method + 18h).

| Read | Value |
| --- | --- |
| B against A | **9216 of 9216 pixels equal** (top byte masked) |
| T's raw words against A | 9144 of 9216 differ |
| 3228h, 2100h, 400100h, 400700h | no pusher error, no interrupts, PGRAPH idle |
| 100C90h | `050D0FFF`, no VM trap |

**The window works.** The same physical pages, addressed through the
kind-70h window, were written in a different order from the linear one:
almost every raw word is somewhere else (72 coincide where the two
orders agree). Read back through the same window and out to a linear
surface, every pixel is right. This is the double mapping with
different kinds that nouveau never does, and was the plan's main risk.

The copy out is the present path: a tiled back buffer to the linear
primary.

## Step 3: the 3D engine draws into a tiled target

`V9XN5CH /lean /3d /z:tiled2d /z:tiledrt` (`z3b`, f5097622). Three
kicks, each waited for: a 502D copy fills the tiled surface T with the
pattern from a linear surface; the 3D engine draws phase 2's solid
rectangle into T as a block-linear render target (tile mode 10h in
`RT_ADDRESS`, `RT_HORIZ` width and height, `RT_ARRAY_MODE` 1, as EXA
programs its own target), addressed through the kind-70h window; a 502D
copy brings T out to a linear surface.

**960 of 960 rectangle pixels are the colour, 8256 of 8256 others the
pattern**, every kick clean, no VM trap. So the 3D engine renders into a
tiled target through the alias window, and the present copy shows the
result.

The first run of the stage (`z3`, c65b1e59) stopped the pusher on the
copy-out (3228h `A00035E4`, error 5; `PFIFO_INTR` 1000h), after a clean
fill and draw. It was the probe's bug, not the card's: the stage began
an op list only once, so the third kick's run replayed the earlier
kicks' ops, their PUT writes included, and moved PUT backwards. Each kick
now begins its own list.

## Not covered

- The Z windows (6Ch, 28h): nothing has addressed them yet.
- 16 bpp and 8 bpp surfaces through the window, other tile modes, and
  surfaces whose height is not a multiple of the block height.
- Gouraud or textured draws into a tiled target (only the solid one).
