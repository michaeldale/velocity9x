# GT218: a tiled-kind alias window works, measured by a 2D round trip

Date: 2026-11-14

Steps 1 to 4 of [docs/plans/nvidia-gt218-3d-z.md](../plans/nvidia-gt218-3d-z.md),
on M-BXMFA8 (GeForce 210, GT218 A8), boots 117 and 118. Evidence:
`build/driver-results/m-bxmfa8-gt218-3d/z1-V9XN5CH.INI` and
`z2-V9XN5CH.INI` onward, per step below.

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

## Step 4: depth buffers through the Z windows

The stages `/z:depth16` and `/z:depth24` draw into the tiled target T
(128x72, tile mode 10h) with a Z buffer of the same size and tile mode
at window offset 3E0000h, addressed through the kind-6Ch (Z16, format
13h) or kind-28h (Z24S8, format 16h) window. The draw: `ZETA_ADDRESS`,
`ZETA_ENABLE`, `ZETA_HORIZ`, depth test LESS with writes, a depth clear
to 1.0, then two 40x24 rectangles from a vertex program that passes z
through (`v9x_nv50_3d_depth_vp`, result map 41060100h): near red at
z 0.25 drawn first, far green at z 0.75 offset by (20, 12). T is then
copied out and compared.

**First run (`z5`, 322c1b24 before the fix, boot 117): a PROP trap.**
`PGRAPH_INTR` 200000h, trap 100h; TP0's PROP status `80000004`, bit 2
SURF_WIDTH_OVERRUN; TP1 clean. Its other words: E1Ch 80h, E20h D0000h,
E24h 08000000h, the rest 0. Hypothesis taken: EXA's setup leaves the
screen scissor at 8192 by 8192, and the depth clear covers the screen
scissor (Mesa clears with `CLEAR_RECT_VIEWPORT` after
`nv50_validate_fb` has set `SCREEN_SCISSOR_HORIZ`/`VERT` (FF4h) to the
framebuffer's width and height), so the clear ran past the 128-pixel
surface. The colour-only stages never cleared, which would explain why
they never tripped it. Which of the clear and the draws overran was not
isolated.

**With `SCREEN_SCISSOR` set to (128<<16, 72<<16) in
`v9x_nv50_push_3d_zeta` (boot 118, 322c1b24-dirty; `z8`, `z9`):**

| Format | Red | Overlap red | Green | Pattern | Bad | Traps |
| --- | --- | --- | --- | --- | --- | --- |
| Z16 | 960 | 240 | 720 | 7536 | 0 | none |
| Z24S8 | 960 | 240 | 720 | 7536 | 0 | none |

The 240 overlap pixels kept the near rectangle's red although green was
drawn after it, so the depth test runs and the clear set the buffer far.
Both Z windows' kinds work as the format expects; whether a mismatched
kind would trap (ZETA_STORAGE_TYPE_MISMATCH, 200h) was not tried.

## Not covered
- 16 bpp and 8 bpp surfaces through the window, other tile modes, and
  surfaces whose height is not a multiple of the block height.
- Gouraud or textured draws into a tiled target (only the solid one).
