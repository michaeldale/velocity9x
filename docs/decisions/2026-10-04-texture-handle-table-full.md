# 3DMark 99 Game 2 was dark because the texture handle table filled

Date: 2026-10-04. Machine: A8U4I5, ATI 3D Rage XL PCI (`1002:4752`),
Mach64 engine path. Closes
`docs/issues/2026-10-04-mach64-3dmark-game2-dark-world-white-sprites.md`.
Evidence: `docs/probe/a8u4i5-rage-xl-pci-2026-10-03/game2/`.

## Finding

The D3D core held 256 texture handles across all contexts
(`V9X_D3D_TEXTURE_COUNT`). Game 2 keeps about 2,690 alive. Once the
table was full, `V9xD3dTextureCreate` returned `DDERR_OUTOFMEMORY` and
nothing counted it. The runtime then bound handle 0 for those textures.
Game 2 draws its lighting as a second pass blended DESTCOLOR / ZERO, so
each untextured lighting pass multiplied the frame by its black diffuse.

| Counter              | 256 entries | 4096 entries |
|----------------------|------------:|-------------:|
| `D3dTextureCreates`  |         306 |        2,737 |
| `D3dTextureDestroys` |          50 |           50 |
| `DrawsNoHandle`      |  about 20,000 |        227 |
| `D3dTextureTableFull`|  not counted |          0 |

`306 - 50 = 256`: the successful creates stopped exactly at the table
size. The 256-entry figures agreed across three runs (19,824 to 20,049
untextured draws). Those runs used an instrumented HAL that is not
committed; the counters above do not depend on its changes.

With 4,096 entries the corridor is lit and textured, the white squares
are gone and the HUD boxes are green again
(`game2-texture-table-4096.png`). The run refused nothing.

## How it was found, and what the evidence killed

Experiments used a temporary HAL that skipped chosen batches:

- Fog off: no change. Fog is not the cause.
- Skipping the DESTCOLOR and SRCCOLOR passes: the world appears, unlit.
  So the base pass was right and a modulating pass blacked it out.
- Specular drawn as colour: walls still black. Not a specular mapping
  fault.
- The lighting batches had black diffuse but real texture coordinates
  (0.25, 0.75, 0.969), and their count matched `DrawsNoHandle`. A
  lightmap was meant to be bound. That pointed at TextureCreate, and the
  create and destroy counters showed the full table.

The Mach64 blend, texture-format and alpha-test hypotheses in the issue
were not the cause.

## Change

- `V9X_D3D_TEXTURE_COUNT` is 4,096 (64 KiB of entries).
- `v9x_d3d_texture_from_handle` checks the handle's address against the
  array instead of walking all entries. It is called on every
  TEXTUREHANDLE state change, so a linear search of 4,096 entries per
  bind was not acceptable.
- The free-slot search starts after the last slot taken.
- A full table increments `texture_table_full`, dumped as
  `D3dTextureTableFull`. Shared ABI stamp 2026100307.

The table lives in the shared D3D core, so this applies to every engine.

## Not established

- Whether 4,096 is enough for everything. Game 2 uses about two thirds of
  it. `D3dTextureTableFull` will show if anything goes beyond that.
- Whether this explains the Intel netbook's missing textures in 3DMark 99
  (`docs/plans/intel-3dmark99-missing-textures.md`, `DrawsNoHandle=30140`).
  The same core served that run, but its create and destroy counts were
  not compared with 256. It needs a run on the netbook.
- The 227 untextured draws that remain were not examined. They may be
  ones the application meant to leave untextured.
- The washed-out Image Quality capture of Game 1 is a separate matter and
  was not looked at.
