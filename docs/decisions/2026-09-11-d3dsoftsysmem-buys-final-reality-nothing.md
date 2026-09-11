# D3DSoftSysMem buys Final Reality nothing, because its textures never go there

Date: 2026-09-11. Measured on `Win98SE-Fast-D3D` - Celeron 533, Voodoo3 3500
AGP on the `vbe` package, `Direct3D=2` - boots 587 to 589.

[The system-memory texture record](2026-09-10-software-d3d-system-memory-textures.md)
closed owing a timed rung: the option was proven correct with a pixel and
never measured through the driver, and the guess behind it was that texel
reads across the aperture are what a textured draw waits for. The
[fast guest](../vm-environment.md) then measured exactly that - an aperture
read costs what the aperture costs and a 2.7x faster CPU bought 1.4x on a
video-memory target - so this was the machine on which the option should have
been worth the most.

It is worth nothing here, and the reason is more useful than a number.

## What was measured

Final Reality 1.01, Robots only, every other test cleared, one pass, read off
FR's own 3D tests page:

| `D3DSoftSysMem` | Boot | Robots |
|---|---|---|
| off | 587 | 0.67 images/s |
| **on** | 588 | 0.53 images/s |
| **on** | 588 | 0.54 images/s |
| off | 589 | **0.53 images/s** |

The second and third rows were taken first and read as a 21% regression. The
fourth row is why this record exists: with the setting **off** again, the
same scene reads 0.53. Three of the four runs agree within 2% and they span
both settings, so **0.67 is the outlier - the first run after a fresh boot -
and the setting moves nothing.** Visual appearance reads 66.67% in all four.

A single run either side of a switch would have published a 21% regression
that is not there. Robots on this guest needs the spread measured before any
difference under about 25% means anything.

## Why it moves nothing

The option is working. `V9XHW.INI` reads `D3DSoftSysMem=allowed`,
`EngineCaps` carries `0x00000130` where the off boots read `0x00000030` - bit
`0x100` is `V9X_DD_ENGINE_CAP_D3D_SOFT_SYSMEM` - and the probe's
`SysMemTexRaw` reads 2016, green, so the engine samples a system-memory
texture on this path.

It has nothing to act on, and the **off** boot is what proves it. When the
option is off, a system-memory texture is refused and counted in
`texture_refused_sysmem`. Boot 587 ran the whole Robots scene with 349
textures created and:

```
D3dTextureRefusedSysmem  0
D3dTextureRefusedFormat  0
D3dTextureRefusedOther   0
```

Every refusal counter zero with the option off means **DirectDraw never
placed one of Final Reality's textures in system memory**. Advertising
`D3DDEVCAPS_TEXTURESYSTEMMEMORY` tells the runtime the driver *can* sample
one; it does not make the runtime choose that placement, and for this
application it does not. The textures sit in video memory either way, the
sysmem arm is never entered, and the aperture reads the fast guest measured
are still being paid.

The probe's `SysMemTex` cell sees a difference only because it asks for
`DDSCAPS_SYSTEMMEMORY` explicitly - which the cell's own comment says it does
on purpose, "because the question is whether the *driver* can sample one, not
which placement DirectDraw favours". That distinction is the whole of this
result.

## What this does and does not settle

**The owed timed rung is answered for one application.** For Final Reality,
the answer is zero, and the cause is placement policy rather than sampling
cost. The option's own record should no longer be read as promising a speed
-up that merely had not been measured.

**It does not say the option is useless.** It says the driver cannot buy the
win by advertising alone. An application that asks for system-memory textures
itself would get it, and the fast guest's aperture numbers say the win would
be real when it happens. What is not established is whether any period
application does that.

**Nothing here tried to force the placement.** Whether the driver could
*decline* video-memory texture placement and push the runtime towards system
memory - and whether that is a sane thing for a display driver to do - is
untouched, and is the next question if this option is to earn its keep.

**One card, one application, emulator only.** The Voodoo3's AGP aperture is
already the fastest here, which is the least favourable case for the option;
the same test on the ViRGE/DX, whose aperture reads are half as quick, is
cheap and has not been run.

## Gates

No code changed. Verified on `Win98SE-Fast-D3D`, agent 9878, with
`Direct3DMode=software` so every Direct3D draw is served by the CPU.
