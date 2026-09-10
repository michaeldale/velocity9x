# The software engine can sample a system-memory texture, behind a setting

Date: 2026-09-10. Implemented and verified through the installed driver on the
86Box Trio64 guest, boots 339 to 344. No timing claim: what is measured here
is correctness.

The scalar plan's non-scalar finding says the software rasterizer's dominant
cost is probably not its arithmetic but where its texels live - the engine
refused any `DDSCAPS_SYSTEMMEMORY` texture, so every textured pixel read one
or four texels across the PCI aperture. This adds the option to place them
where the CPU reads them cheaply, off by default, and proves the new path with
a pixel.

## What it is

`[Velocity9x] D3DSoftSysMem=1` in `SYSTEM.INI`, read at Enable in the same
section and by the same call as `Direct3D` and the `GdiAccel` keys. Absent is
off, so no machine changes behaviour without asking.

The 16-bit driver publishes the answer two ways: as
`V9X_DD_ENGINE_CAP_D3D_SOFT_SYSMEM` in the engine capability word, which is
the channel that already selects the software engine, and as
`D3DSoftSysMem=allowed|refused|not-applicable` in `V9XHW.INI` - three states,
because "you turned it off" and "this machine is not running the software
engine" are different facts.

The engine then does two things with it. `describe_caps` publishes
`D3DDEVCAPS_TEXTURESYSTEMMEMORY` beside the video-memory cap, so DirectDraw
knows the placement is available rather than being told about one the driver
would refuse. And `v9x_d3d_soft_texture_setup` grows a second addressing arm:
a video-memory surface is still named by its offset into the aperture and
bounded against it, while a system-memory surface's `fpVidMem` is a linear
address in the calling process - which is where the callback runs - and is
used as it stands.

That second arm is why this is a setting and not a default. A video-memory
texture is contained by an explicit check against the aperture; a
system-memory one can only be bounded by its own pitch and extent, which
`v9x_d3d_raster_texture_valid` checks. The guarantee is weaker, so the choice
is the operator's.

## Verified with a pixel, and it took three tries

The probe gained a cell that asks for a texture with `DDSCAPS_SYSTEMMEMORY`
explicitly - not left to the runtime's preference, because the question is
whether the driver can sample one - fills it green, and draws it opaque with
`COPY` over a `0x18E3` fill.

| Setting | `SysMemTexRaw` | |
|---|---|---|
| `refused` | 65535 | white: the vertex colour, so the draw went untextured |
| `allowed`, first attempt | 65535 | still white |
| `allowed`, after the fix below | **2016** | green, equal to `D3DExpectGreen` |

Create, `GetHandle` and the draw itself returned success in every one of
those, which is the point: nothing in the HRESULTs distinguishes the three
rows.

## The bug the second row was hiding

`engine_caps` is stamped in **two** places - `v9x_dd_stamp_engine_caps`, which
also runs at block allocation before DriverInit, and `v9x_dd_refresh_framebuffer`,
which runs on every DirectDraw session setup and rewrites the word from
scratch. The new bit went into the first only, so the second erased it every
time. `V9XHW.INI` said `allowed` - it reads the setting, not the word - while
the engine went on refusing.

That took a guest A/B to see, and it needed one more thing:

## The software engine now counts its texture refusals

It refused in silence. The ViRGE path has counted its refusals since 3DMark
99, for the reason its own comment gives - a refused texture draws as
untextured Gouraud in the vertex colour, which looks exactly like a texture
full of that colour - and the software engine reached the same trap by the
same route. Every arm of `v9x_d3d_soft_texture_setup` now increments the
existing diagnostics, which the two engines share because a boot runs one of
them and `V9XHW.INI` records which.

With them, the second row above resolved in one read:

```
D3dTextureRefusedSysmem  1
D3dTextureRefusedOther   1
D3dTextureRefusedCaps    0x00001800   DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY
D3dTextureRefusedVidMem  0x004300A0
```

- the surface was the one the cell created, and the arm that refused it was
the new one. After the fix, every refusal counter reads zero on the same run.

## What this does not establish

**No speed measurement through the driver.** The motivation is the benchmark's
RAM-versus-VRAM gap - the same synthetic scenes ran about twice as slow
against a video-memory target, and point-sampled modulate 1.46x against 1.29x
between the two
([record](2026-09-10-rasterizer-texel-units-and-bilinear.md)) - but that
instrument links the rasterizer directly and never loads the driver, and it
measures the *target*, not the texture. What a real application gains from a
system-memory texture on this driver is unmeasured. The probe times no draws;
a timed rung that draws the same textured triangles with the texture in each
placement is what would settle it, and it is owed.

**Nothing physical.** Emulator only, and 86Box's aperture is a model of one.
The plan's step 1 - the physical aperture read timing - remains unrun, and it
is the number that should decide whether this becomes a default.

**One thing seen in passing and not explained.** On this guest in software
mode the probe's `BlendModulate` cell draws white, and it read the same in
both of this morning's pre-change runs, so it is neither new nor caused by
this work. It is not a texture refusal either: every refusal counter reads
zero on the run where it happened.

> **Explained on 2026-09-11, and it was a defect.** Two things in the
> paragraph above are wrong: the cell is not a texture cell and is not
> video-memory-specific. It draws an *untextured* triangle with
> `DESTCOLOR`/`ZERO` - the multiplicative pass a lightmap uses - and the
> software engine drew a blend it cannot express as opaque, painting white
> over the frame, where the hardware path skips and counts. Fixed and
> verified: [the software engine drew a blend it cannot express,
> opaque](2026-09-11-the-software-engine-drew-an-inexpressible-blend.md).

## Gates

`run-checks.ps1` green after every step, including the host tests - the
rasterizer's pixel table is untouched by this, since the change is which
pointer the engine hands it. Verified on `Win98SE-Trio64`, agent 9871, which
has no S3D engine and therefore serves every Direct3D draw from the CPU with
`Direct3D=2`. `build-host-msvc.ps1` cannot run on this host.
