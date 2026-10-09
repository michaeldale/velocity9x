# What four games call through the ICD, and the attribute stacks they did not use this time

Date: 2026-10-09
Machines: `Win98SE-Fast-D3D` (86Box, port 9878, boot 608, software
engine); the netbook (Intel GMA 950, wifi at 10.0.1.254, boot 129).
Driver: each machine's installed display driver and HAL, with V9XGL.DLL
built from this change (`b1002b0-dirty`, 454,656 bytes). 9878 was left on
it. The netbook was put back on its own 0.13.0 ICD (CRC 632D4946), with
the configs Quake 2, GLQuake, Half-Life and UT99 rewrite on exit restored.
A8U4I5 was not used (in use by Michael).
Evidence: [`../probe/icd-census-attrib-2026-10-09/`](../probe/icd-census-attrib-2026-10-09/)

## The change

**A call census.** Until now the ICD counted only calls to stubs, so a
game run could not say which implemented entry points it used. The
dispatch generator (`scripts/lib/gl-dispatch.ps1`) now also emits a
forwarder per slot, which counts the call and calls on with every
argument and the result. The ICD puts the forwarder in front of every
implemented slot. At process exit `V9XGL.LOG` gets `census name=calls ...`
lines and `census implemented-slots-called=N`. Extension entry points
reached through `DrvGetProcAddress` (the SGIS pair) are not in the
table, so they are not counted. The cost is an increment and an indirect
call per GL call. It was not measured on its own.

**The stubs games had hit.** These are `glPushAttrib`, `glPopAttrib`,
`glPushClientAttrib` and `glPopClientAttrib` (a new pure module,
`gl_attrib.c`); `glGetTexParameter{fv,iv}`,
`glGetTexLevelParameter{fv,iv}` and `glGetTexEnv{fv,iv}`; and
`glClearStencil`. Earlier V9XGL.LOGs show Half-Life and Quake 2 calling
`glPushAttrib` and Serious Sam calling `glGetTexLevelParameteriv` and
`glClearStencil`. The ICD now implements 215 of 336 slots.

- A push copies all of the state the ICD keeps. A pop writes back the
  groups the mask named (table 6.30), the enables by group, and
  ENABLE_BIT all of them. TEXTURE_BIT restores each unit's binding,
  environment and enable, the SGIS selected unit and the bound objects'
  filters and wraps. A name deleted since the push is made an object
  again. Groups for state the ICD does not keep yet (stipple, lighting
  parameters, stencil function, evaluators, texgen) save only their
  enables.
- The texture level queries answer from the stored image: base format,
  and component sizes as stored (RGB565 for RGB and luminance,
  ARGB4444 otherwise). Proxy targets and 1D textures get INVALID_ENUM,
  as glTexImage2D gives them.
- The stacks are about 10 KB a context and come from the heap at the
  first push. Kept in the context table, they made the DLL 611,328 bytes
  and the floppy package too large. The linker writes zero-filled
  statics into the image, as
  [the floppy issue](../issues/2026-09-26-floppy-over-capacity-with-opengl-icd.md)
  recorded.

Host tests were written first and seen failing against stubs. They
cover the stack limits and errors, one group only, ENABLE_BIT, every
group round-tripped, TEXTURE_BIT with a deleted name and unit 1, both
client groups, the texture queries and their errors, the new state
queries, and the census forwarder calling on with arguments and
result. The generator change was stashed to watch the dispatch test
fail.

## Measured

**V9XGLP**, with a new section (push COLOR_BUFFER_BIT, change, pop,
clear and read back; underflow; client pixel store; level and
environment queries; the stencil clear value). It passed on both
engines. On 9878 the census logged the probe's own 67 entry points.

**Games on the netbook, the census at exit:**

| Program | Run | Implemented slots called | Stub calls |
|---|---|---|---|
| Half-Life 1.1.1.0 `-gl` | `timedemo mwd5`, 16.794 fps | 41 | 0 |
| Quake 2 demo `+map demo1` | `timerefresh`, 34.557 fps | 43 | 0 |
| GLQuake 0.97 | `timedemo demo1` twice (below) | 42 | 0 |
| UT99 436, OpenGLDrv | the intro flyby, then `exit` | 32 | 0 |

The union is 52 entry points, and 25 are used by all four. None of them
is a fog call, one of 0.14.1's 97 new forms, or an attribute-stack call.
Those are exercised only by V9XGLP so far. `glPushAttrib` is used by
Half-Life and Quake 2 in some sessions (earlier logs) and was not
reached in these. Frame rates are recorded, not compared.

Serious Sam was not run. It stopped at its CD check, because its disc
image was not mounted.

## GLQuake drew to a 1x1 window while it was not in front

The netbook's NETGEAR WG111v3 wizard window held the foreground. On
both GLQuake launches the ICD bound a 1x1 drawable, and GLQuake sat
minimised on the taskbar. The first timedemo reported 969 frames at
16.9 fps with almost everything clipped away. Restored from the
taskbar, GLQuake rebound at 640x480 and played demo1 visibly, at
8.0 fps with the console open over it. Earlier GLQuake runs on the
netbook (0.12.2 ICD) bound 640x480 from the start. The drawable size
comes from the window's client rectangle, which this change does not
touch. The cause read here is the foreground lock. It was not tested
by launching the old ICD under the same conditions.

The white areas beside GLQuake's status bar in the screenshot are also
in the 2026-10-08 recording on the 0.13.0 ICD
(`netbook-multitexture-recheck-2026-10-08`). They predate this change.
Filed as [2026-10-09-glquake-white-beside-status-bar](../issues/2026-10-09-glquake-white-beside-status-bar.md).

## Not measured

- The census forwarders' cost on their own.
- Any game reaching `glPushAttrib` or the texture queries under this
  ICD.
- The Rage XL (A8U4I5) and the ViRGE.
