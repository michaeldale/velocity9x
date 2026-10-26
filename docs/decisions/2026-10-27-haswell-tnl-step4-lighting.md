# HD 4600: vertex kernels generated per key, and OpenGL lighting and fog

Date: 2026-10-27. Machine: the i5-4570 (W9QI5A83, HD 4600), boot 98,
800x600x16 desktop (565). Step 4 of
[`../plans/hardware-tnl.md`](../plans/hardware-tnl.md); follows
[`2026-10-26-haswell-tnl-step3c-no-cpu-twin.md`](2026-10-26-haswell-tnl-step3c-no-cpu-twin.md).
Evidence:
[`../probe/hsw-4570-tnl-step4-2026-10-27/`](../probe/hsw-4570-tnl-step4-2026-10-27/).

## What changed

- **One vertex kernel per T&L key** (`hsw_vs.c`). It replaces step 3's
  fixed position-only kernel for every key, unlit ones included. Each
  kernel is unrolled for its key, with no flow control:
  - transform and eye position;
  - the normal through the inverse transpose, renormalised on request;
  - each light in either API's model: directional, point or spot;
    attenuation and D3D's range; GL's cutoff or D3D's cone pair;
  - the material sources, specular (apart for D3D, summed for GL);
  - fog: linear, exp or exp2, over depth or range;
  - the texture matrices, and the default colours.
- **How a skipped light is handled.** Where the reference evaluator skips
  a light (out of range, a denominator not above zero, N.L not above
  zero), the kernel computes the term anyway and a predicated SEL
  replaces it with zero, NaN included.
- **Only the constants a kernel reads are pushed.** The generator records
  which vec4s of the 1,440-byte block it reads, and the HAL packs just
  those.
  - Unlit kernels push 2 GRFs, as step 3's did.
  - The largest key builds to 7,264 bytes and reads 37 GRFs: eight
    lights, two-sided, specular, exp2 fog, two texture matrices.
- **The Haswell reserve grows from 128 KB to 256 KB** (`intel_hsw.h`,
  `loader.asm`). The first 128 KB is unchanged; after it come:
  - a cache of eight 8 KB vertex-kernel slots, keyed by the key's bytes;
  - per draw slot, 32 KB holding the normals stream and every segment's
    packed push constants.
  - The constants were in each segment's 64-byte share of the vertex
    area's tail before. A lit kernel's no longer fit there.
  - The DirectDraw heap ends 128 KB lower: 1EB0000h on the 4570.
- **How the cache is validated.** Each cache slot carries a tag in the
  status page; the directory is a static (shared, as the HAL's statics
  are). A ring open zeroes the page, so a reset reserve matches nothing.
  A miss drains the render engine before rewriting a slot, as a pixel
  kernel's slot is rewritten.
- **Normals: render interface ABI 8.** A T&L draw carries object-space
  normals when its key reads them.
  - The VF fetches them as a sixth element, R32G32B32_FLOAT, from vertex
    buffer 1.
  - The TL vertex keeps its 32 bytes.
  - The Glide and 3DCIF wrappers negotiate the same version, so they must
    ship with this HAL.
- **OpenGL lighting** (`gl_light.c`, new). It covers:
  - `glLight`, `glLightModel`, `glMaterial`, `glColorMaterial`, and
    `glGetLight` and `glGetMaterial`;
  - light positions and spot directions transformed by the model-view at
    the call;
  - the state kept on the heap, a const initial state standing in until
    the first call (the static contexts are in the DLL's file, and the
    floppy has no room).
  - The capture records each vertex's current normal, and the element
    cache keeps it too.
  - A lit key reads the normals; the key builder fills the lights in
    LIGHT0 to LIGHT7 order, the normal matrix and the sources.
- **OpenGL fog is captured now.** It goes over |z_e|, the distance the
  CPU path's fog coordinate already was. A linear fog whose start equals
  its end is one everywhere, which no fog gives.

## Correctness

- **Host.** `test_intel_hsw_vs.c` runs each kernel in a host model of the
  EU subset (`hsw_eu_sim.c`) over twelve vertex pairs, against
  `r3d_tnl_ref.c`.
  - Covered: unlit, the default colours, fog in all three modes over
    depth and range, both texture matrices, D3D's three light types with
    specular, local viewer, normalise, colour sources and no normals,
    GL's cutoff spots, two-sided, colour material and the largest key.
  - Replacing the condition in any of the five gates (range, GL cutoff,
    D3D outer cone, D3D inner cone, specular's N.L) with equality fails
    tests, so each one is exercised.
  - What this shows: the program computes the reference's equations with
    the registers it names. What it does not show: the EU's precision, or
    that the model's reading of the ISA is the hardware's.
- **4570, before lighting** (the new HAL with the step 3c ICD's capture):
  - V9XGLP's A/B scene gives the same numbers as at boots 85, 87 and 88:
    1,800 pixels differ, worst channel 9/255, none past 16.
  - 225 T&L triangles, 0 refused, 0 failed.
- **4570, lit probe** (`V9XGLP`'s new Lit scenes, boot 98). The probe
  evaluates GL 1.1's equation in its own code and reads back the centre
  of 40 4x4-pixel samples over five scenes:
  - the default light;
  - a coloured directional light with specular, shininess 8;
  - an attenuated point light;
  - a 30 degree spot with exponent 4, samples inside and outside it;
  - two lights with colour material, a local viewer, specular 20 and
    normals of length 3 under `GL_NORMALIZE`.
  - **All 40 match within 10/255.** The largest differences are 4 or 5
    levels, which is 565 rounding.
  - The fog tests (linear, exp, at the eye) pass through T&L.
  - `glGetLight` returns a position transformed by the model-view current
    at the call.
  - 313 T&L triangles, 0 refused.

## Speed

Half-Life GL `timedemo mwd5`, windowed, with the new HAL and the step 3c
ICD (boot 98):

| Run | fps |
|---|---|
| First after the boot | 80.4 |
| Second | 96.7 |

Step 3c's settled figures were 95.9 and 95.5. The generated unlit kernel
costs nothing measurable. The first run after a boot is slow, as on every
boot since 88.

Quake 3 `timedemo demo001` with the lighting ICD: 61.0 fps against step
3c's 61.1, 2.3 million T&L triangles, none refused.

## Not done

- **Two-sided lighting.** The kernel writes back colours, and the host
  tests check them, but SBE's facing select is not programmed.
  - The HAL refuses a two-sided key, and the ICD does not capture one.
  - Such primitives stay unlit, as all OpenGL lighting was before.
- **`glPushAttrib(GL_LIGHTING_BIT)`** saves the lighting enables but not
  the light and material parameters.
- **Other chips.** They still draw OpenGL unlit; lighting needs an engine
  that runs T&L.
- **A colour material on the back face alone** leaves the front's
  sources: the key has one set of sources for both faces.
- **Calls between Begin and End.** A `glMaterial` there flushes what was
  captured first, so a triangle spanning the change takes the new
  material.
- **Untested here:** any game that uses OpenGL lighting. None measured
  here does, which is why the plan made the probe the gate.
