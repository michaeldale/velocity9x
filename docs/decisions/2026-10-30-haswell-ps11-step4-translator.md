# Pixel shader 1.0/1.1 on Gen7.5: parser, reference interpreter and translator, held to each other on the host

Date: 2026-10-30. Step 4 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Host work only: nothing here has run on the HD 4600.

## What was built

- **The parser** (`src/display32/d3d/d3d_ps11.c`, `include/velocity9x/ps11.h`):
  - accepts PS 1.0 and 1.1 token streams, with comments and def;
  - the arithmetic set: mov, add, sub, mul, mad, lrp, dp3, cnd, nop;
  - the texture set: tex, texcoord, texkill, texbem, texbeml and the
    texm3x3 family (pad, tex, spec, vspec);
  - refuses, so CreatePixelShader can fail rather than draw wrong:
    texreg2ar, texreg2gb, the texm3x2 pair, texm3x3diff, anything from
    1.2 on, and every register, mask, swizzle, modifier, shift, ordering
    or count rule 1.1 does not allow.
- **The reference interpreter** (`d3d_ps11_ref.c`, host only, as
  `r3d_tnl_ref.c` is): ddi8-on-win9x.md section 6 in plain C.
  - Results are clamped to [-1, 1], or saturated; constants are clamped
    where they are read.
  - dp3 replicates; cnd tests `r0.a > 0.5`; a co-issued pair reads its
    sources before either writes.
  - Three choices where the sources leave a gap, made the same way in
    the translator: texbeml's luminance multiplies the colour channels
    only, unclamped; projection applies to tex and texbem only; every
    cube lookup divides by its largest magnitude.
- **The Gen7.5 translator** (`src/chipsets/intel/hsw_ps11.c`,
  `include/velocity9x/intel_hsw_ps11.h`), SIMD8 Align1 with one register
  per channel, as the fixed-function kernels are:
  - def constants are folded to immediates at build time;
  - c0-c7, the four bump matrices and the luminance terms come in seven
    push-constant registers;
  - six attributes are interpolated with pln;
  - stage N samples through binding table entry 1 + N and sampler N;
    a cube's coordinates are divided by their largest magnitude first;
  - texkill keeps the smallest killing coordinate, and the render target
    write is predicated on it being non-negative.
- **The host EU model** (`tests/host/hsw_eu_sim.c`) now runs pln, a
  sampler send answered by a test callback, and the render target write
  with the channels it enabled.

## How it was checked

`tests/host/test_d3d_ps11.c` holds the parser: six accepted shaders and
21 refusals. `tests/host/test_intel_hsw_ps11.c` builds ten kernels:

- the instruction patterns 3DMark 2001 SE's shaders use: tex, cube tex,
  texkill, texm3x3pad + texm3x3vspec on a cube, texbem with a co-issued
  pair;
- the rest of the set: texbeml on a projected stage, texm3x3spec,
  texm3x3tex on a volume, and every arithmetic instruction and source
  modifier with shifts, saturation, a def and clamped constants;
- cnd.

Each runs on eight pixels in the EU model and through the interpreter.
r0 must agree within 2e-4 and the same pixels must be killed. The
lookup both sides use is one smooth function of the coordinates, so the
check is the kernel's logic, not a sampler's.

**The tests fail when they should.** Two deliberate breaks, swapping the
bump matrix's m01 and m10 and inverting the kill test, failed 3 and 8
pixels' checks; the translator was then restored.

## Kernel sizes

| Kernel | Bytes |
|---|---|
| tex | 512 |
| tex, cube | 624 |
| texkill | 624 |
| cnd | 656 |
| texbeml, projected | 944 |
| texbem with a co-issued pair | 976 |
| texm3x3tex | 1,024 |
| texm3x3vspec, cube | 1,424 |
| texm3x3spec, cube | 1,664 |
| eight arithmetic instructions, every modifier | 2,544 |

Today's fixed-function slots are 768 bytes. Shader kernels need their own
region (the plan's design). A 1.1 shader's worst case, eight arithmetic
instructions each with three modified sources on four channels, is not
bounded here: step 5 sizes slots from what programs make, and refuses a
kernel that does not fit.

## Not done, not checked

- **Nothing ran on the GPU.** The EU model is this tree's reading of the
  ISA: the encodings round-trip through the decoder, but the model does
  not reproduce the hardware's float precision, and it is not
  cycle-accurate.
- **Constants:** the translator assumes the push block holds c0-c7
  already clamped to [-1, 1]. Filling it so is step 5's job.
- **Fixed-function fog** after the shader, and alpha test on r0.a, are
  not in the kernel yet (step 5).
- **The SIMD8 sampler message** with three coordinates (cube, texm3x3),
  and the predicated EOT write, follow the PRM as
  ddi8-on-win9x.md section 7 reads it. Its open questions stand: the
  SIMD8 parameter order beyond u and v, and what a predicated EOT send
  does with every channel off.
- The translator is not linked into the HAL yet; step 5 adds it.
