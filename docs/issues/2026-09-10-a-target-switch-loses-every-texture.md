# A render-target switch silently loses every texture

Filed: 2026-09-10
Status: **CLOSED the same day, refuted.** The driver is right and the probe was
wrong: the runtime retires its own texture handles when it destroys the context
to perform the switch, and re-creates them on the next `GetHandle`. An
application that caches handle values across a target switch is using values
the runtime has retired, which is what this rung did. Re-fetching the handles
after the switch makes the same draws land - wall green, blend a clean ramp -
with no driver change
([record](../decisions/2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md)).
Component: none. `tools\diag\ddraw_probe_win32.c` now re-fetches.

## What was seen, and what it meant

The chain rung's draws landed untextured after the switch: `ChainWallRaw`
`0x7FFF`, the white vertex colour, where the wall texture is green (`0x03E0`,
which the `Solo_*` rung reads), and `ChainSpriteAlpha=0` against the solo
rung's `1`. The texture table is keyed by (handle, context) and
`ContextDestroy` drops every record of its context, so the reading fitted a
driver that had thrown the application's textures away.

The measurement that decided it, boot 580 with a build that kept the records
past `ContextDestroy`:

```
ChainTexDestroys   2     the runtime called TextureDestroy itself, twice
ChainTexCreates    0     ... and created nothing while nothing asked it to
ChainWallRaw   32767     still white: keeping the records changed nothing
```

So the runtime is not relying on the driver to hold those records - it retires
its handles deliberately. Then, with the handles re-fetched by `GetHandle`
after the switch and the shipping driver restored:

```
ChainRehandleDstHr / SrcHr   0x00000000      both re-fetched
ChainTexCreates              2               the runtime re-created them
ChainWallRaw               992               green: textured
ChainSpriteAlpha             1
Chain_x12 .. Chain_x48     930 806 682 620 464 341 217
```

The ramp, matching `Solo_x12..x48` exactly. Boot 581, `V9XHAL.DLL` 44,032.

One detail worth keeping: the handle *values* were unchanged across the switch
(`2957024348` and `2957024364` both times), because the records were freed and
the same two slots re-used. A stale handle that still resolves by value is why
the pixel keys looked like a blend problem for three sessions.

## What the driver kept from the attempt

Only `v9x_d3d_textures_forget_surface`, called from `V9xHalDestroySurface`
beside the colour-key equivalent, and justified on its own terms rather than
by this issue: the runtime destroys texture handles with their context, not
with their surface, so an application that releases a texture's surface
without a `TextureDestroy` left a record holding a pointer to a freed `lpLcl`
for the sampler to read. That is reachable today and is now closed.

The per-process keying the attempt introduced is reverted. The DDK's pairing -
a texture belongs to the context that created it - is correct, and the
measurement above is why.
