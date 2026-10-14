# A 5:5:5 desktop and 3D on the ATI family

Date: 2026-10-14

Status: Phase 0 done on the Rage IIC. The 555 desktop boots there by
default, and the Rage II draws 1555 targets:
[desktop](../decisions/2026-10-14-rage-iic-555-desktop.md),
[3D](../decisions/2026-10-14-rage-iic-draws-1555-targets.md). On the Rage
II, untagged XRGB1555 textures draw, and Glide opens at 555 and converts
the LFB ([boot 464](../decisions/2026-10-14-rage-iic-555-textures-and-glide.md)).
The 2D width code is left at 565 on purpose (same record). Phase 3: the
Rage XL measured alike at both layouts, and the Rage Pro class now
chooses 555 ([boot 469](../decisions/2026-10-14-rage-xl-555-desktop.md)).
Remaining:
- Phase 1's blend step on the Rage IIC (a SCALE_3D_CNTL sweep).
- The Gateway's Rage Mobility booted at 555.

Phase 2 is done on the Rage IIC: Diablo II and the NFS II demo were
checked on the monitor. Chosen by Michael on 2026-10-14 over keeping
565 and correcting 555-only titles in ATI3DCIF.DLL.

## Why

ATI's own driver runs 16 bits as 5:5:5. The evidence:
- marxveix reports it as the default, with `SYSTEM.INI [Display]
  five6five=1` selecting 565.
- Under ATI's driver on A8U4I5, V9XDDP measured a 555 Direct3D target
  (`D3DTargetRMask=31744`, boot 450).
- Register samples taken under ATI's driver show the Rage IIC drawing with
  DP_PIX_WIDTH destination code 3, which is 1555
  (`docs/probe/a8u4i5-rage-iic-registers-2026-10-02/hl-sample-ati/ATIRX-SAMPLE.TXT`).

Titles written for ATI cards assume it:
- Cubes clears its background with the 555 value `4210`
  (`docs/probe/a8u4i5-rage-iic-3dcif-phase3-2026-10-14/README.md`).
- marxveix reports that Tomb Raider's ATI patch and the Knight demo need
  555.

Velocity9x runs every ATI chip at 565 today.

## Where 565 is assumed

From a survey of the tree on 2026-10-14:
- **The mode set.** The layout is decided in one place:
  - `v9x_highcolor_resolve` (`src/common/vbe_modes.c`) gives 555 only
    when Direct3D is hardware and `v9x_dd_engine_wants_555`
    (`src/display16/dd16.c`) says yes, and that is true for the ViRGE
    alone.
  - The ATI mode set is the VBE BIOS alone, so 555 means the 15 bpp
    sibling mode (0x110/0x113/0x116 for 0x111/0x114/0x117).
  - Three places must then agree, as for the ViRGE: the VBE mode, the
    `FIVE6FIVE` flag and the published masks
    (`docs/decisions/2026-09-02-a-555-desktop-needs-three-places-to-agree.md`).
- **2D (`mach64_engine.c`).**
  - DirectDraw fill and copy program DP_PIX_WIDTH with code 4 (565).
  - Engine init writes DP_CHAIN_MASK `0x8410`, the 565 value.
  - There is no ATI GDI acceleration to change.
- **Rage II 3D.**
  - `rage2_draw.c` refuses any target but 565 and emits
    `V9X_R2_DP_PIX_WIDTH_565`.
  - `rage2_trap.c`'s scene builders do the same.
  - Fog colour and the interpolators are format-free.
  - Dither is off. ATI's driver has it on (SCALE_3D_CNTL low nibble
    `F`).
- **Mach64 3D.**
  - `mach64_policy.c` refuses any target but 565 ("XRGB1555 waits for its
    own scene").
  - `mach64_engine.c`'s flat-state builder hard-codes DP_PIX_WIDTH
    `0x40040444`, and every later builder inherits it.
  - The draw state carries no target format.
- **The neutral layers.**
  - Describe offers the Rage II no target unless the desktop is 565, and
    offers the Mach64 a 555 target that its policy then refuses.
  - The Rage II and Mach64 texture classifiers refuse XRGB1555 textures,
    including untagged textures that inherit a 555 display format.
  - Glide refuses to open without a 565 target. Its LFB lock, read and
    write are 565.
  - The OpenGL ICD and ATI3DCIF.DLL already handle a 555 target.
- **Tests that pin 565 for ATI:** `test_rage2_draw.c`,
  `test_rage2_trap.c`, `test_mach64_policy.c`, `test_d3d_mach64_map.c`
  and `test_mach64_engine.c`.

## Phases

### 0. Measure on A8U4I5's Rage IIC

1. **The BIOS mode.** The VBE cache does not show 15 bpp modes (it keeps
   28 of the BIOS's 40). `[Velocity9x] HighColor=15` in SYSTEM.INI forces
   the 555 layout regardless of the engine. Does 1024x768 come up on
   0x116, with `is555=1` and a correct desktop?
2. **2D at 555.** DirectDraw fill and copy with DP_PIX_WIDTH code 3. Both
   are bit-copies; the measurement is whether they stay exact.
3. **3D at 555.** A Rage II scene with DST code 3: flat, Gouraud and
   textured triangles (565, 1555 and 4444 texels), and a blend, read back
   and compared with the same scene at 565. The scene tool is
   `tools/diag/ati_rage2_scene_win32.c`. Also check whether DITHER_EN
   matters at 15 bits.

**Gate:** a decision record with the boot logs and scene readbacks.

### 1. The layout and the engines, test-first

- `v9x_dd_engine_wants_555` true for ATI_RAGE2 and, once Phase 3
  measures it, ATI_MACH64.
- The target format carried from the request into the Rage II and
  Mach64 draw state, and DP_PIX_WIDTH's destination field set from it.
- The 2D engine's DP_PIX_WIDTH and DP_CHAIN_MASK set from the desktop
  format.
- Policy accepts XRGB1555 targets. Describe offers what the engine
  draws.
- XRGB1555 textures accepted where the engine samples them. Phase 0
  measures whether the Rage samples a 1555 texel with its top bit ignored.

**Gate:** build-host and run-checks; A8U4I5 boots 555, and Direct3D,
OpenGL and ATI3DCIF draw. Cubes clears grey.

### 2. Glide at 555

Glide accepts a 1555 target, converts LFB writes and reads between the
game's 565 and the target, and reports the write mode the lock gives.

**Gate:** Diablo II and Rollcage on the Rage XL as before, colours
checked on the monitor.

### 3. The Mach64 (Rage XL, Rage Pro, Mobility)

The same scene measurements on the Rage XL, then `wants_555` for
ATI_MACH64. Until then the Mach64 class stays 565.

**Gate:** a decision record; the Rage XL and the Gateway's Rage Mobility
boot 555 and draw.

## Hazards

- **No VT2 change.** The VT2 stamps no engine type and stays 565.
- **Users with a stored HighColor.** A SYSTEM.INI `HighColor=16` keeps
  565; that is the escape hatch, and it stays.
- **Engine-dependent layout.** Glide titles that write the LFB in 565
  will cost a conversion per write.
- **Every ATI user's desktop changes layout.** That goes in the CHANGELOG.
