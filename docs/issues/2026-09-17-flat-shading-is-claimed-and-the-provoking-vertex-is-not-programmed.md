# Flat shading is claimed, and the provoking vertex is not programmed

Date: 2026-09-17
Status: RESOLVED in the core, 2026-09-18. `SHADEMODE` is retained and under
`D3DSHADE_FLAT` the core copies the first vertex's colour and specular to
the other two before any engine draws, so the Gouraud interpolator produces
Direct3D's flat result and the provoking-vertex control below stays
unprogrammed and irrelevant. Both flat caps are true from that build. The
record below stands as written; it is why the fix is in the core.

`v9x_d3d_i9xx_describe_caps` publishes `D3DPSHADECAPS_COLORFLATRGB`, so an
application may set `D3DSHADEMODE_FLAT` and expect a triangle to take one
vertex's colour. Direct3D says which one: the **first** vertex.

Intel's 945G datasheet 307502-005 says the hardware has a choice, and that it
is programmable:

> Flat shading provoking vertex is selectable by state variable for D3D vs
> OpenGL.

(recorded in `C:\everything\claude\personal\intel driver research\
gen3-915-945-datasheets\945G-datasheet-307502-notes.md`, from section 10.5.)

**This driver never sets it.** `V9X_I9XX_S5_PHASE5` is zero and no constant
for a flat-shade or provoking-vertex control exists anywhere in
`intel_gen3_3d.h`. Whichever convention zero selects is the one every draw
gets, and nothing here has established which that is - D3D's first vertex or
OpenGL's last.

## The alpha cap, declined for the same reason (2026-09-18)

When blending reached the runtime path, `D3DPSHADECAPS_ALPHAFLATBLEND` was
briefly claimed beside `ALPHAGOURAUDBLEND`. Review caught it: the cap
promises the first vertex's alpha across a flat-shaded triangle, and this
path passes each vertex's alpha through unchanged - the same defect as the
colour cap above, now with a visible consequence, since a flat triangle
with differing vertex alphas would vary in transparency. It is withdrawn.
The core does not retain `D3DRENDERSTATE_SHADEMODE` at all; when flat
shading is built - the state retained, the provoking vertex programmed or
the first vertex's colour and alpha copied to the other two in the core -
both caps become true together.

## Why it has not been seen

Every triangle this project has drawn through the runtime path has had one
colour on all three vertices, or has been Gouraud shaded where the provoking
vertex does not arise. The two are indistinguishable in a single-colour
triangle, which is every triangle measured so far - the same blind spot that
hid the rhw rule, and for the same reason.

## What it would look like

A flat-shaded triangle in the colour of the wrong corner. Plausible, not
obviously wrong, and invisible to any comparison that does not use three
different vertex colours.

## What to do

Measure before changing anything. One scene, three vertices, three distinct
colours, `SHADEMODE_FLAT`, and a probe at the centre: the result names the
provoking vertex directly. The bit to program, if it turns out to be the
OpenGL convention, has to come from Mesa's i915 headers - the datasheet says
a state variable exists and does not say which one.

Related: `D3DPSHADECAPS_COLORGOURAUDRGB` is also claimed and also unmeasured
for the same reason. A three-colour triangle tests both at once.
