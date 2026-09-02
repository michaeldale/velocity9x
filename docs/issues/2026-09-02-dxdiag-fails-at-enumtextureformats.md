# DxDiag's Direct3D test fails at step 44, EnumTextureFormats

Filed: 2026-09-02
Status: open, reported from a physical machine, cause not established
Reported by: the user, from A8U4I5's own DirectX Diagnostic Tool

## What it says

DxDiag's **Test Direct3D** on A8U4I5, running the `s3` package with hardware
Direct3D on an S3 Trio3D/2X:

```
Test failed at step 44 (EnumTextureFormats): HRESULT = 0x00000000 (error code)
```

The HRESULT is **zero**. DxDiag calls the step failed while the call it names
succeeded, which means it is judging the *result* of the enumeration rather than
its return code - it asked for texture formats, got `S_OK`, and did not find
what it needed.

The Display tab is otherwise healthy: `Name: Velocity9x S3 Trio3D/2X`,
`Manufacturer: Velocity9x`, `Main Driver: v9xdisp.drv`, DirectDraw Acceleration
Enabled, Direct3D Acceleration Enabled.

## The lead: no RGB565 texture format is published

The probe records what the enumeration returns, and has on every run on every
target:

```
TexEnumHr=0x00000000      the call succeeds
TexFormatCount=2          two formats offered
TexFormat1555=1           ARGB1555 present
TexFormat4444=1           ARGB4444 present
TexFormat565=0            RGB565 absent
```

Both engines publish exactly two formats, ARGB1555 and ARGB4444, and **no
plain RGB565** - see `v9x_d3d_virge_describe_caps` and
`v9x_d3d_soft_describe_caps`. The display on this machine is 16-bit RGB565. A
conformance test that expects a texture format matching the display format, or
simply expects RGB565 to be among those offered, would fail exactly here with
exactly this shape: a successful call whose contents it rejects.

That is a hypothesis with a cheap test and it has **not** been run.

## Why this is not obviously the same as the black screen

Texturing works. `D3DBaseTextureOk=1` and `Tex4444PixelOk=1` on the software
engine, and the hardware path samples both formats with the raw values the
emulated ViRGE produces
([record](../decisions/2026-09-02-trio3d-on-the-s3-path.md)). Final Reality
created 723 textures during its run and scored. So applications that ask for one
of the two published formats are served.

What is unknown is what an application does when it wants a format this driver
does not publish. DxDiag declines to continue; a game might fall back, might
pick a wrong format, or might render nothing.

## Next

1. **Establish what step 44 wants.** DxDiag's D3D test is a fixed sequence; the
   step number is stable across runs and machines, so the same test against the
   retail S3 driver on the same box would show whether it passes there and with
   what format list.
2. **Ask whether RGB565 is publishable.** For the software rasterizer this is a
   sampler change and nothing else - it already decodes two formats and a third
   is arithmetic. For the ViRGE path it is a hardware question: the S3D texture
   unit selects its format from the command register, and whether it has an
   RGB565 texel mode at all needs the DDK or 86Box's model to answer, not a
   guess.
3. **Do not publish it before it is implemented.** The rule this driver keeps -
   advertise nothing a pixel test cannot hold you to - applies with force here,
   because the failing test is one that reads the advertised list.
