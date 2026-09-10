# A render-target switch silently loses every texture

Filed: 2026-09-10
Status: **OPEN, cause measured and read in the source, fix not made** - it is a
change to how the driver's texture table is keyed, and there are two ways to
make it.
Severity: high for any application that switches render target. Every draw
after the switch loses its texture, with no error and no counter moving.
Component: `src/display32/d3d/d3d_core.c` -
`v9x_d3d_textures_destroy_context`, `v9x_d3d_texture_from_handle`,
`V9xD3dContextDestroy`.

## What happens

A texture record is keyed by the pair (handle, context):

```c
if ((DWORD)&v9x_d3d_textures[index] == handle &&
    v9x_d3d_textures[index].active != 0ul &&
    v9x_d3d_textures[index].context == context)
```

and `V9xD3dContextDestroy` calls `v9x_d3d_textures_destroy_context`, which
marks every record belonging to that context inactive.

This runtime performs `IDirect3DDevice2::SetRenderTarget` by destroying the
HAL context and creating another one on the new surface - it never calls
`V9xD3dSetRenderTarget`
([record](../decisions/2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md)).
So the switch drops every texture the application created, the application's
handles are unchanged and still valid as far as the runtime is concerned, and
the lookup above fails for all of them. The draw proceeds untextured.

## Measured

Emulated ViRGE/DX, `Win86SE`, agent port 9869, boot 578, 640x480 at 5:5:5,
`V9XDISP.DRV` 45,562 bytes. The probe's chain rung, whose depth buffer is now
cleared and whose vertices carry a wall depth and a nearer sprite depth, so
nothing here is the depth test:

```
ChainSetTargetCalls   0     the driver's SetRenderTarget was never called
ChainCtxDestroys      1     the runtime destroyed the context
ChainCtxCreates       1     ... and created another
ChainTexCreates       0     and did NOT re-create its textures
ChainTexDestroys      0     nor destroy them; it expects the handles to hold

ChainWallRaw      32767     white: the vertex colour, untextured
SoloWallRaw         992     green: the same kind of draw, textured, on a
                            device created on the back buffer

ChainSpriteAlpha      0     the driver saw no texel alpha to blend with
SoloSpriteAlpha       1     the control: it did
```

32767 is `0x7FFF`, white in this 5:5:5 mode; 992 is `0x03E0`, the green the
wall texture is filled with. The chain rung's wall is drawn with the same
handle it used before the switch, and it comes out the vertex colour.

No counter moves for it: `ChainSprite_Dref` and `ChainSprite_Dskip` are
absent, so nothing was refused and no blend was skipped. A handle that
resolves to no texture is not counted anywhere - the draw is simply untextured.

## Why the pixel keys could not see it

The chain rung's wall texture is green and its sprite is a white-ish alpha
ramp, and an untextured draw takes the white vertex colour. So "the blend left
no mark" and "both draws painted white" produce the same seven readings, and
the rung read them as the former for three sessions. `texture_alpha_draws`,
bracketed around the one draw, is what separated them.

## Two ways to fix it, and why neither is obviously right

1. **Stop dropping the records on `ContextDestroy`.** Closest to what this
   runtime evidently assumes: it hands the handles back out unchanged and
   never re-creates. The cost is lifetime - the records would then outlive
   their context and be reclaimed only by `TextureDestroy`, `ContextDestroyAll`
   or a process teardown, and a title that leaks handles would exhaust
   `V9X_D3D_TEXTURE_COUNT`.
2. **Stop keying the lookup by context.** The record already carries its
   surface, and a handle is the address of the record, so the context term is
   not what makes a handle unique. The cost is that a stale handle from a
   *different* application would resolve rather than fail, which the context
   term currently prevents.

The DDK creates textures with a context handle
(`V9xD3dTextureCreate(data->dwhContext, ...)`), which is where the pairing
came from, and it is not wrong on its face - it is wrong in combination with a
runtime that rebuilds the context under the application.

## What it might explain

3DMark 99's black boxes on the Trio3D are a sprite-shaped region that is drawn
and black
([issue](2026-09-03-3dmark99-on-the-trio3d-after-the-stride-fix.md)). An
untextured sprite blended over black would be exactly that shape, and 3DMark
renders to a chain. This is a hypothesis and nothing more: that machine has
not run any of this, and the counter that would settle it - `ChainSpriteAlpha`
or its equivalent during a 3DMark run - has never been read there.

## Next

1. Decide between the two fixes above, then measure the chain rung again: the
   wall must read 992 and `ChainSpriteAlpha` must read 1.
2. The `Chain_x*` samples should then show the ramp, as `Solo_x*` now does.
3. Only after the emulator agrees, the same rung on silicon.
