# GT218 Tesla 3D: the method list for a first triangle

Date: 2026-11-14

Phase 0 of [docs/plans/nvidia-gt218-3d.md](../plans/nvidia-gt218-3d.md).
Desk work from source; **nothing here has run on a card**. Phases 1 and 2
build from this list and record what the card did.

## Sources, as fetched

In `build\external\nv50\` (not committed, MIT-licensed):

- `mesa\`: Mesa's `nv50` gallium driver (`nv50_screen.c`, `nv50_3d.xml.h`,
  `nv50_state*.c`, `nv50_shader_state.c`, `nv50_program.c`, `nv50_vbo.c`,
  `nv50_tex.c`) and the Tesla encoder `nv50_ir_emit_nv50.cpp`, through the
  Fuchsia mirror of Mesa (`fuchsia.googlesource.com/third_party/mesa`,
  `?format=TEXT`, base64). The GitHub `mesa3d/mesa` path answered 404.
- `xf86-nouveau\`: xf86-video-nouveau's `nv50_accel.c`, `nv50_accel.h`,
  `nv50_exa.c`, from freedesktop's GitLab (raw, master).
- `envytools\`: `rnndb\graph\g80_3d.xml`, `g80_texture.xml`.

## The class

8597h on the GT218: xf86-video-nouveau picks `NVA3_3D_CLASS` for every
A0-series chipset but A0, AA, AC and AF (`NVAccelInitNV50TCL`), and
nouveau's kernel lists `GT214_TESLA` (8597h) for `gt215_gr`.

## What the minimal pipeline needs, and what it does not

xf86-video-nouveau's EXA composite is the smallest complete pipeline in
the sources. Read against Mesa's `nv50_screen_init_hwctx`, it shows what
a first triangle can leave out:

- **No stack, no local memory (TLS), no code segment base.** EXA's
  programs use neither, and Tesla programs are placed by absolute
  address (`VP_ADDRESS_HIGH`, `FP_ADDRESS_HIGH`) with an entry offset
  (`VP_START_ID`, `FP_START_ID`). Mesa's `LOCAL_ADDRESS` and
  `STACK_ADDRESS` are for compiled GLSL.
- **No upload engine.** EXA writes its programs, TIC, TSC and constants
  into a scratch buffer with `PUSH_DATAu` (an inline upload). Our probe
  can write the same words into the reserve through BAR1.
- **No microcode.** The context image is nouveau's
  ([trace record](../decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md)).

One constraint matters for Direct3D later: **a pitch-linear colour
buffer cannot be paired with a depth buffer.** Mesa programs a linear
render target with `RT_HORIZ = RT_HORIZ_LINEAR | pitch` and a tile mode
of 0, and asserts `!fb->zsbuf` when it does
(`nv50_state_validate.c`, `nv50_validate_fb`). EXA refuses linear
targets outright ("Scanout buffer not tiled"). DirectDraw's surfaces are
all pitch-linear, so a Z-buffered Direct3D draw needs a tiled
(block-linear) colour target, and that needs VM page entries with a
tiled storage type. Our identity VM writes storage type 0 everywhere.
This decides the shape of phase 4 and is unmeasured.

## Phase 1: bind and set up (EXA's `NVAccelInitNV50TCL`)

The 3D object goes into RAMHT and the instance block beside 502D, as
`nv50_chan.c` already does for 502D, with class 8597h. Then, on its own
subchannel, every method EXA sends (offsets from `nv50_3d.xml.h`):

| Method | Offset | Value |
| --- | --- | --- |
| object bind (subchannel) | 0000h | the 3D object's handle |
| `COND_MODE` | 1558h | 1 (always) |
| `DMA_NOTIFY` | 0180h | a null object in EXA; leave out, or bind the VM DMA object (to measure) |
| `DMA_ZETA` and the 10 after it | 0184h | the VM DMA object, 11 times |
| `DMA_COLOR(0..7)` | 01C0h | the VM DMA object, 8 times |
| `RT_CONTROL` | 121Ch | 1 |
| `VIEWPORT_TRANSFORM_EN` | 192Ch | 0 (vertices are window coordinates) |
| `COLOR_MASK_COMMON` | 0F90h | 1 |
| `ZETA_ENABLE` | 1538h | 0 |
| `TIC_ADDRESS_HIGH`, low, limit | 1574h | TIC table address, `800h` |
| `TSC_ADDRESS_HIGH`, low, limit | 155Ch | TSC table address, 0 |
| `LINKED_TSC` | 1234h | 1 |
| `TEX_LIMITS(2)` | 13BCh | `54h` |
| `VP_ATTR_EN(0..1)` | 1650h | 3, `33h` (attributes 0, 8 and 9, x and y) |
| `VP_REG_ALLOC_RESULT` | 16B8h | 6 |
| `VP_RESULT_MAP_SIZE`, temp | 16ACh | 8, 4 |
| `VP_ADDRESS_HIGH`, low | 0F7Ch | vertex program address |
| `CB_DEF_ADDRESS_HIGH`, low, buffer | 1280h | VP constants address, `(1 << 16) \| 256` |
| `SET_PROGRAM_CB` | 1694h | `1 \| (1 << 12)` |
| `VP_START_ID` | 140Ch | 0 |
| `VP_RESULT_MAP(0..1)` | 16BCh | `41400100h`, `05040302h` |
| `POINT_SPRITE_ENABLE` | 1520h | 0 |
| `FP_INTERPOLANT_CTRL`, temp | 1988h | `08040404h`, 8 |
| `FP_ADDRESS_HIGH`, low | 0FA4h | fragment program address |
| `CB_DEF_ADDRESS_HIGH`, low, buffer | 1280h | FP constants address, `(2 << 16) \| 256` |
| `SET_PROGRAM_CB` | 1694h | `31h \| (2 << 12)` |
| `SCISSOR_ENABLE(0)` | 0E00h | 1 |
| `VIEWPORT_HORIZ(0)`, vert | 0C00h | `8192 << 16`, `8192 << 16` |
| `SCISSOR_HORIZ(0)`, vert | 0E04h | `8192 << 16`, `8192 << 16` |
| `SCREEN_SCISSOR_HORIZ`, vert | 0FF4h | `8192 << 16`, `8192 << 16` |

Mesa sends more (`REG_MODE` striped, `WATCHDOG_TIMER` 18h, the
multisample and clip-rectangle defaults, `TEX_MISC` for NVA0+, the
auxiliary constant buffer and vertex runout). They are the first
additions if EXA's list alone traps or draws wrongly.

Every address above is a VM address, the identity of a video memory
offset. All of them go in the reserve, so phase 1 grows it: the
programs (EXA's layout: VP at +0, FP at +1000h, TIC +2000h, TSC +3000h,
VP constants +4000h, FP constants +4100h) need 16.5 KiB beyond the
channel's 704 KiB, which still fits the 1 MiB reserve. The phase 4
decision on the reserve size stands.

## The programs

Copied word for word from `nv50_accel.c` with EXA's own disassembly.
They are data for the probe, not code we wrote:

- **Vertex program**, 30 instructions (60 words) at VP: passes
  attribute 0 (window x, y) to the position outputs, and transforms
  attributes 8 and 9 (source and mask coordinates) by the 3x3 matrices
  in VP constants 0 to 0x54, with a divide, into outputs 2 to 5.
- **`PFP_S`**, 3 instructions (6 words) at FP + 0: interpolate
  coordinates, one `texauto` from texture 0, exit. The colour is the
  texel.

For a solid colour, EXA binds a 1x1 texture holding the colour
(`NV50EXAPictSolid`): one TIC entry (8 words: format `8_8_8_8` with
B, G, R, A swizzle, the texel address, `d005d000h` with the high
address, `300000h`, 1, `10001h`, `3000000h`, 0) and one TSC entry
(repeat on all axes, nearest, no mip). No new shader encoding is
needed for the first triangle.

## Phase 2: one solid rectangle

Per draw, from `NV50EXAPrepareComposite` and `NV50EXAComposite`, with
Mesa's linear target in place of EXA's tiled one:

1. `RT_ADDRESS_HIGH(0)` (0200h), 5 words: address high, low, format
   (`BGRA8_UNORM` at 32 bpp, `B5G6R5_UNORM` at 16), tile mode 0,
   layer stride 0.
2. `RT_HORIZ(0)` (1240h), 2 words: `80000000h | pitch`, height.
3. `RT_ARRAY_MODE` (1224h): 0 (linear; EXA's tiled target uses 1).
4. `BLEND_ENABLE(0)` (1360h): 0.
5. The solid texel, TIC 0 and TSC 0 written to the reserve; the VP
   constants for unit 0 (identity matrix, 1/width, 1/height).
6. `FP_START_ID` (1414h): 0 (`PFP_S`).
7. `TIC_FLUSH` (1334h): 0. `BIND_TIC(2)` (1458h): 1, then `203h`.
8. `SCISSOR_HORIZ(0)` (0E04h), 2 words: `(x + w) << 16 | x`,
   `(y + h) << 16 | y`.
9. `VERTEX_BEGIN_GL` (15DCh): 4 (triangles). Three vertices, each
   `VTX_ATTR_2I(8)` (06A0h) with two packed source and mask coordinates,
   then `VTX_ATTR_2I(0)` (0680h) with `y << 16 | x`. EXA draws one
   triangle twice the rectangle's size and lets the scissor cut it.
   `VERTEX_END_GL` (15E0h): 0.

The check is the 2D engine's: read the rectangle and its border back
through BAR1, byte for byte, at 32 and 16 bpp.

## Open points for phase 1 to measure

- Whether `DMA_NOTIFY` can be left unset, or needs a null object.
- Whether a linear target draws with `RT_ARRAY_MODE` 0, as Mesa sends,
  and EXA's two `BIND_TIC(2)` writes mean what they appear to (unbind,
  then bind texture 0 with sampler 0).
- Whether EXA's list is complete on top of nouveau's context image, or
  needs Mesa's extra defaults. EXA ran on the same context image, so
  it should be, but on a channel nouveau had also used for 2D.
