/*
 * Engine identity and capability values.
 *
 * These are the one vocabulary shared by all three execution contexts: the
 * 16-bit chip modules that fill the engine descriptor, the 16-bit driver that
 * clamps its DirectDraw caps from it, and the flat 32-bit HAL that selects its
 * engine ops from it. They are therefore in a header of their own, with no
 * includes and no dependency on <windows.h>, so that
 * include\velocity9x\hw16.h can stay free of the OS boundary and
 * include\velocity9x\win9x_ddraw_abi.h no longer has to reach into the
 * 16-bit hardware layer to find them.
 *
 * engine_type names the chip's drawing engine and selects a code path.
 * engine_caps says what that engine will do and is a mask, so a chip can
 * carry an engine with only part of the family's capability set.
 */
#ifndef VELOCITY9X_ENGINE_ABI_H
#define VELOCITY9X_ENGINE_ABI_H

#define V9X_DD_ENGINE_TYPE_NONE         0ul
#define V9X_DD_ENGINE_TYPE_S3_VIRGE_DX  1ul
#define V9X_DD_ENGINE_TYPE_S3_TRIO64    2ul
/*
 * Intel Gen3, meaning the GMA 950 on the 945GSE and nothing wider.
 *
 * 3 is the next free value. Assigning it does NOT publish anything: the
 * intel-gma family manifest still declares EngineType NONE, and the engine
 * behind this value reports itself not ready. What the value buys is that the
 * selector arms exist and are tested, so what remains is the engine body
 * rather than the plumbing around it.
 */
#define V9X_DD_ENGINE_TYPE_INTEL_GEN3   3ul
/* ATI Mach64 VTB+ engine. Declared for Phase 1 plumbing; ATI manifests remain
 * NONE until the physical 2D/3D gates publish a measured capability set. */
#define V9X_DD_ENGINE_TYPE_ATI_MACH64   4ul
/*
 * ATI Rage II class (264GT2C, the Rage IIC): the Mach64 register window and
 * 2D engine, but no triangle setup engine. A type of its own because the
 * selectors route by type, and ATI_MACH64 reaches d3d_mach64.c, whose every
 * triangle is a setup-engine packet this chip cannot take
 * (docs\decisions\2026-10-02-rage-iic-register-survey.md).
 */
#define V9X_DD_ENGINE_TYPE_ATI_RAGE2    5ul
/*
 * SiS 6326 2D engine, MMIO through BAR1: solid fill and screen copy, built by
 * src\chipsets\sis\sis6326_engine.c and measured byte-exact on the card at 8
 * and 16 bpp (docs\decisions\2026-10-05-sis6326-2d-engine-writes.md); and
 * the 3D engine, src\display32\d3d\d3d_sis6326.c, which d3d_select.c gives
 * this type (docs\decisions\2026-10-05-sis6326-3d-*.md).
 */
#define V9X_DD_ENGINE_TYPE_SIS_6326     6ul
/*
 * Matrox MGA-2064W drawing engine, MMIO through the 16 KiB control aperture
 * in BAR0: solid fill and screen copy, built by src\chipsets\matrox\
 * mga_engine.c. The chip has no 3D engine; d3d_select.c gives this type
 * none, and software Direct3D still wins when it is selected.
 */
#define V9X_DD_ENGINE_TYPE_MGA_2064W    7ul

#define V9X_DD_ENGINE_CAP_SOLID_FILL    0x00000001ul
#define V9X_DD_ENGINE_CAP_SCREEN_COPY   0x00000002ul
#define V9X_DD_ENGINE_CAP_FLIP          0x00000004ul
#define V9X_DD_ENGINE_CAP_VBLANK        0x00000008ul
#define V9X_DD_ENGINE_CAP_D3D           0x00000010ul
/*
 * Serve Direct3D from the CPU rasterizer rather than from the chip.
 *
 * Set by the 16-bit driver when [Velocity9x] Direct3D selects the software
 * mode, on any chip, and never by a chip module - no silicon has this
 * capability, the driver does. It is what the 32-bit HAL selects the software
 * V9X_D3D_ENGINE_OPS on, and it implies V9X_DD_ENGINE_CAP_D3D so that the
 * existing clamp keeps publishing the Direct3D tables.
 *
 * That makes this word carry a mode decision and not only a chip fact, which
 * is a departure from the header comment above - and the departure predates
 * this bit: mode 1 already clears CAP_D3D from a user setting. The rule that
 * survives is the useful one: the 16-bit side is the single authority on what
 * may be advertised, whether it is reading silicon or a setting.
 */
#define V9X_DD_ENGINE_CAP_D3D_SOFTWARE  0x00000020ul
/*
 * Two passes over one triangle, blended together, produce the right answer on
 * this part.
 *
 * A chip fact, and the first one this word has carried that is a negative. The
 * engine synthesises trilinear filtering that way - level N, then level N+1
 * blended over it at the LOD fraction - and on the emulated ViRGE/DX the
 * result is a clean mix of the two levels, three times over. On the S3
 * Trio3D/2X it is not: the second pass lands wrong, and it does so on a boot
 * where every single-pass alpha blend the probe can measure is correct
 * (docs\decisions\2026-09-04-the-trilinear-two-pass-and-a-retraction.md).
 *
 * The bit is deliberately about the two-pass form and not about alpha. An
 * earlier version of it said the part could not blend at all; a controlled A/B
 * on one machine, two boots apart, disproved that and is recorded in the
 * decision above. What survives the A/B is only this: on a part without this
 * bit, trilinear has to degrade to a filter the chip does in one pass rather
 * than emit a second pass whose result is wrong.
 */
#define V9X_DD_ENGINE_CAP_S3D_TWO_PASS  0x00000040ul
/*
 * A draw with TEXTURE_UNLIT and the alpha field enabled keeps its texel's
 * colour on this part.
 *
 * The ViRGE/DX does. The Trio3D/2X samples the texel's alpha, honours it, and
 * drops the colour: an opaque texel draws black over whatever was there, while
 * an alpha-zero texel correctly leaves the destination alone. Measured over
 * eight cells crossing depth, filter and shade mode, where depth and filter
 * made no difference and the shade mode made all of it
 * (docs\decisions\2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md).
 *
 * TEXTURE_UNLIT *without* the alpha field is correct on both parts, so this is
 * about the pair and not about the unlit path in general.
 */
#define V9X_DD_ENGINE_CAP_S3D_UNLIT_ALPHA 0x00000080ul

/*
 * Let the CPU rasterizer sample a texture that lives in system memory.
 *
 * Set by the 16-bit driver from [Velocity9x] D3DSoftSysMem, and meaningless
 * without V9X_DD_ENGINE_CAP_D3D_SOFTWARE beside it: no chip samples system
 * memory, the CPU does.
 *
 * Off by default, and the default is the conservative one rather than the
 * fast one. The software engine has always refused a DDSCAPS_SYSTEMMEMORY
 * texture, because it reaches a texture through the framebuffer aperture and
 * a system-memory surface is not in it. That refusal costs: every textured
 * pixel then reads one or four texels through the PCI aperture, and the same
 * synthetic scenes measured about twice as slow against a video-memory
 * target as against a RAM one on the 86Box guests
 * (docs\decisions\2026-09-10-rasterizer-texel-units-and-bilinear.md).
 *
 * What the bit buys is the *option*, published to DirectDraw as
 * D3DDEVCAPS_TEXTURESYSTEMMEMORY, so the runtime may place a texture where
 * the CPU can read it cheaply. What it costs is the containment check: a
 * video-memory texture is bounded against the aperture, and a system-memory
 * one can only be bounded by its own pitch and extent. That is why it is a
 * setting and not the default, and why the default stays where the evidence
 * is (docs\plans\software-rasterizer-scalar-fixes.md, the non-scalar finding).
 */
#define V9X_DD_ENGINE_CAP_D3D_SOFT_SYSMEM 0x00000100ul
/*
 * The Intel flip goes through the ring (MI_DISPLAY_FLIP) rather than a
 * plane-base register write. Stamped beside CAP_FLIP whenever the ring is
 * up; the HAL falls back to the register write without the ring.
 * docs\plans\intel-gen3-ring-flip.md.
 */
#define V9X_DD_ENGINE_CAP_FLIP_RING     0x00000200ul
/* Gen3 runtime streams may return after publishing RING_TAIL. This policy
 * bit is set with runtime 3D unless IntelAsyncSubmit reads 0 (2026-10-01).
 * The descriptor's existing caps word carries it without growing the shared
 * block. */
#define V9X_DD_ENGINE_CAP_ASYNC_SUBMIT  0x00000400ul
/*
 * The user's [Velocity9x] VSync setting, as two policy bits. Neither set is
 * the application's choice, today's behaviour. Stamped by the 16-bit driver
 * on every chip, in both places that build this word, and read by the HAL's
 * flip path through include\velocity9x\vsync.h. A policy bit removes or
 * forces a wait; it grants no flip the chip cannot do.
 * docs\plans\vsync-off-setting.md.
 */
#define V9X_DD_ENGINE_CAP_VSYNC_ON      0x00000800ul
#define V9X_DD_ENGINE_CAP_VSYNC_OFF     0x00001000ul
/*
 * The DirectX 6 driver interface: GUID_D3DCallbacks3 (DrawPrimitives2),
 * GUID_ZPixelFormats and the DX6 extended caps, which together are what the
 * Direct3D 8 runtime needs before it uses hardware at all
 * (src\display32\d3d\d3d_dp2.h). Policy bits from [Velocity9x]
 * Direct3DDdi, stamped by the 16-bit driver only beside CAP_D3D and in both
 * places that build this word:
 *
 *   D3D_DP2      DDI 6 may be offered. Set unless Direct3DDdi=5.
 *   D3D_DP2_ALL  offer it to every process (Direct3DDdi=6).
 *
 * With D3D_DP2 alone the 32-bit side decides per process, when the runtime
 * negotiates (v9x_d3d_dp2_for_process in d3d_core.c): DDI 6 for a program
 * that has loaded D3D8.DLL, or that [Velocity9x.Direct3DDdi] lists with 6;
 * DDI 5 for the rest, or for one listed with 5. Per process because DDI 6
 * is not a feature beside the DX5 path but a replacement for it: a runtime
 * that finds DrawPrimitives2 sends a DX5 game through it as well, which
 * cost Half-Life 30 % before the Clear2 fix
 * (docs\decisions\2026-10-06-ddi6-runtime-call-rate.md), while a Direct3D 8
 * program gets no hardware device at all without it.
 */
#define V9X_DD_ENGINE_CAP_D3D_DP2       0x00002000ul
#define V9X_DD_ENGINE_CAP_D3D_DP2_ALL   0x00004000ul
/*
 * Intel Gen3: this part samples a mip chain with every level stacked below
 * the one before, the 915's layout, rather than the 945's level-2-beside-
 * level-1. A chip identity bit, stamped by the 915GM's descriptor only;
 * MAP_STATE has no field that selects it. Desk evidence (Mesa's is_i945),
 * unmeasured on a 915: docs\decisions\2026-10-08-intel-915gm-gma900-support.md.
 */
#define V9X_DD_ENGINE_CAP_I9XX_MIP_STACKED 0x00008000ul
/*
 * A DIAGNOSTIC: capture the DrawPrimitives2 calls of the next Direct3D
 * program to C:\V9XDIAG\V9XDP2R.BIN (src\display32\d3d\d3d_dp2_ring.h,
 * read with tools\diag\dp2ring.py). From [Velocity9x] Dp2Capture=1, only
 * beside D3D_DP2. The bring-up's other probe bits (1 to 64, 2026-10-06)
 * were removed on 2026-10-07 once DDI 6 worked; this one stayed because
 * it is how docs\decisions\2026-10-06-ddi6-runtime-call-rate.md was
 * measured.
 */
#define V9X_DD_ENGINE_CAP_DP2_CAPTURE   0x00200000ul

#endif /* VELOCITY9X_ENGINE_ABI_H */
