# GT218 Tesla 3D: depth buffers, tiled surfaces, and presenting them

Date: 2026-11-14

Status: decisions taken (Michael, 2026-11-14): present by copy, a 2 MiB
reserve, Z16 and Z24S8. Step 1 built (b013f697): the alias windows and
the 2 MiB reserve, host-tested. On M-BXMFA8 boot 117 the channel probe
built with them passed every stage it has, 2D and 3D
(`build/driver-results/m-bxmfa8-gt218-3d/z1-V9XN5CH.INI`), so the extra
PDEs and the 2 GiB DMA limit break nothing. Step 2 passed (bd4cddce):
a gradient copied by 502D into a block-linear surface through the
kind-70h window and back is exact, and the tiled surface's raw words are
in a different order, so the double mapping works
([record](../decisions/2026-11-14-gt218-tiled-alias-window.md)). Step 3
passed (f5097622): the 3D engine draws a solid rectangle into a tiled
target through the window, exact after the copy out. Next: step 4, Z.
Part of phase 3 of
[nvidia-gt218-3d.md](nvidia-gt218-3d.md), after solid, Gouraud and
textured draws ran exact on M-BXMFA8 into pitch-linear targets
([texture record](../decisions/2026-11-14-gt218-3d-texture.md)).
Everything below is read from source; nothing is measured.

## The constraint

**A pitch-linear colour target cannot have a depth buffer.** Mesa
programs a linear target only with no Z (`nv50_validate_fb` asserts
`!fb->zsbuf`), and xf86-video-nouveau never renders to one at all. A
Z-buffered draw needs a tiled (block-linear) colour target and a tiled
depth buffer.

Tiled surfaces are read and written by the GPU through VM pages whose
**storage type** ("kind") says how the memory is laid out. Our VM maps
the first 512 MiB of video memory as the identity with kind 0, pitch,
everywhere. Mesa's kinds, compression off (`nv50_mt_choose_storage_type`,
masking `~0x180`):

| Surface | Kind |
| --- | --- |
| colour, 16 or 32 bit | 70h |
| Z16 | 6Ch |
| Z24 with S8 (S8Z24) | 18h |
| Z24S8 | 28h |

A kind goes in bits 46:40 of a PTE, so bits 14:8 of its high word
(nouveau `nv50_vmm_valid`: `map->type |= kind << 40`).

Tiled surfaces are made of GOBs 64 bytes wide; a level's tile mode sets
its block height (Mesa `nv50_tex_choose_tile_dims`: 0, 10h, 20h, 30h, 40h
for 1 to 16 GOBs high), and its pitch is a multiple of 64 bytes.

The display is set by the VBE and scans out pitch-linear memory. So a
tiled back buffer is never shown directly: it is copied to the linear
primary by the 2D engine, which reads a block-linear source
(`SRC_LINEAR` 234h = 0, the block dimensions at 238h).

## The design proposed

**Alias windows in the VM.** The page directory gains entries above the
512 MiB identity window that map the same video memory again, each with
one kind:

| VM range | Maps video memory | Kind |
| --- | --- | --- |
| 0 to 512 MiB | 0 to 512 MiB | 0, pitch (as now) |
| 512 MiB to 1 GiB | 0 to 512 MiB | 70h, tiled colour |
| 1 to 1.5 GiB | 0 to 512 MiB | 6Ch, Z16 |
| 1.5 to 2 GiB | 0 to 512 MiB | 28h, Z24S8 |

A surface's kind is then chosen by which window the GPU addresses it
through; nothing in the VM changes after the channel is built, so no TLB
flush is ever needed at run time (run 1's hang was blamed on a raw TLB
flush, and none has been issued since). Each window is one more 64 KiB
page table in the reserve: 192 KiB for three, which with the channel's
704 KiB and the 3D scratch still fits the 1 MiB reserve, narrowly. The
reserve-size decision in the parent plan comes due here.

The CPU always sees the raw bytes through BAR1, whatever the kind. It
must never write a tiled surface as if it were linear; clears and copies
go through the engines.

**Presenting.** Direct3D's back buffer and Z buffer are tiled, allocated
from the DirectDraw heap with a 64-byte pitch and a tile mode; a flip or
a blit to the primary is a 2D-engine copy, block-linear to pitch, which
the HAL's 502D code already nearly has (`v9x_nv50_push_blit` writes the
source format and layout and leaves 238h alone).

## Steps

1. **The alias windows, host-tested.** `nv50_chan.c` writes the extra
   PDEs and page tables, the kind in each PTE; the host test holds the
   words to nouveau's encoding. The probe's `/check` proves the channel
   still runs (2D fill and copy unchanged).
2. **A 2D round trip through a tiled surface**, no 3D: the probe fills a
   linear pattern, has 502D copy it into a kind-70h tiled surface and back
   to linear, and compares. This proves the alias window, the tile mode
   and the 2D block-linear path before any 3D depends on them, and is
   the present path.
3. **A tiled colour target**: phase 2's solid rectangle drawn into a
   tiled target through the 70h window, copied out by step 2's path.
4. **Z**: a Z16 buffer through the 6Ch window, cleared by
   `CLEAR_DEPTH` (D90h) and `CLEAR_BUFFERS` (19D0h, bit 0); a vertex
   program that writes z (EXA's result map forces z to 0.0, so the map's
   third byte names a real output); `ZETA_ADDRESS_HIGH` (FE0h, 5 words),
   `ZETA_ENABLE`, `ZETA_HORIZ` (1228h, 3 words), `DEPTH_TEST_ENABLE`
   (12CCh), `DEPTH_WRITE_ENABLE` (12E8h), `DEPTH_TEST_FUNC` (130Ch, LESS
   201h). The check: two overlapping rectangles at two depths, the near
   one drawn first; where they overlap, the near one's colour must
   remain. Then Z24S8 the same way.
5. **The HAL**: tiled back buffers and Z buffers in the heap, flips and
   blits by step 2's copy, `Lock` on a tiled surface through a linear
   staging copy.

## Decisions owed (Michael's)

1. **Present by copy.** Direct3D renders into tiled surfaces and every
   flip is a 2D copy to the linear primary. The alternative, a tiled
   primary, would need the display engine programmed instead of the VBE,
   which this card's driver does not do. Recommended: the copy.
2. **The reserve.** Three alias windows take 192 KiB. Recommended: grow
   the reserve to 2 MiB now (1.6% of the 126.5 MiB heap) rather than run
   the 1 MiB reserve to its last 100 KiB.
3. **Which depth formats.** Z16 and Z24S8 cover what Direct3D 5 to 7
   games ask for. Recommended: those two; Z32 and stencil-only later if a
   game needs them.

## Risks

- The alias window assumes the hardware accepts the same physical pages
  mapped twice with different kinds. nouveau never does this (each
  buffer has one kind), so step 2 is where it is measured.
- Tile mode and block height for render targets come from Mesa's
  miptree code, not from a measurement.
- `Lock` on a Z buffer: some games read or write it. A staging copy
  handles colour; a Z16/Z24 untile through 502D may not exist, and
  those locks may have to fail.
