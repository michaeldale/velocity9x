# Rage IIC: the Mobility's register window and 2D engine, but no setup engine

Date: 2026-10-02
Machine: A8U4I5, 10.0.1.172, ATI 3D Rage IIC AGP `1002:4757` rev `7A`,
4 MiB, on ATI's community 4.11.2611 driver, boot 125.
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
(read-only `ATIIC` probe, two runs, plus the shadowed video BIOS).

A survey made before any Velocity9x work on this chip. It covers what the
ati family's Mobility-M code assumes and how much of that holds here. The
desk research was done the same day, and its sources are listed at the end.
Nothing was written to the card.

## Measured

| Question | Measurement | Source |
|---|---|---|
| Identity | `CONFIG_CHIP_ID` = `7A004757`: type `4757`, class 0, revision byte `7A`, the PCI revision | both runs |
| Register window | Config Manager assigns a 4 KiB memory range at `DE000000`, live in BAR2 (cfg `18h`). `CONFIG_CHIP_ID` reads the same through it and through BAR0 + `7FF800h` | `Bar2ChipId`, `ApertureChipId` |
| BAR0 | `DC000000`, 16 MiB | Config Manager, cfg `10h` |
| BAR1 | I/O `D000` (cfg `14h` = `D001`); Config Manager's allocated configuration lists only `3B0h` and `3C0h` | cfg dump |
| Block 1 | `BUS_CNTL` = `7B330040`: `EXT_REG_EN` set by ATI's driver | `+4A0` |
| Memory size | `MEM_CNTL` = `10631577`, code 7. The 4-bit `CTL_MEM_SIZEB` table gives 4 MiB, the installed size; the 3-bit table would give 16 MiB | `+4B0` |
| Memory type | `CONFIG_STAT0` [2:0] = 4, SDRAM, agreeing with the BIOS's "SDRAM" banner. The Mobility reads 6 | `+4E4` |
| Command FIFO | `FIFO_STAT` = 0. `GUI_STAT` = `00C00000`: [25:16] = 192 at idle with `GUI_CNTL` = 0 (`CMDFIFO_SIZE_MODE` 0). The Mobility read 128 | `+710`, `+738`, `+178` |
| Rage Pro setup engine | `VERTEX_1_S` to `VERTEX_3_X_Y`, `ONE_OVER_AREA` and `SETUP_CNTL` all read 0. On the Mobility under ATI's driver, `SETUP_CNTL` read `31000000` | `ATIIC-BLOCK1.TXT` |
| Rage II 3D registers | Hold ATI's last values: `RED_START` `00FF0000`, `GREEN_START` `009B0000`, `BLUE_START` `00320000`, `Z_X_INC` `1FF949EC`, `Z_Y_INC` `00049F08`, `Z_START` `19AAB1C3`, `ALPHA_START` `00FF0000`; S/T at `+74C`, `+754`, `+768`, `+76C`; trailing-edge Bresenham at `+538`-`+544`; `TEX_0_OFF`-`TEX_10_OFF` a descending mip chain; `Z_OFF_PITCH` `14026100` | block 0 |
| AGP | Capability at `50h`, status `FF000001`: 1x only. Power management at `5Ch` | cfg dump |
| BIOS | `ATI MACH64 SDRAM BIOS 3.096`, part `MACH64GWPCIMTSDU`, 1998/09/23, 32 KiB, PCIR `1002:4757`, OEM string `MACH64GT` | `ATIIC.ROM` |

Two offsets, `+0CC` and `+4F8`, read back their own physical address
(`DE0000CC`, `DE0004F8`). That looks like the undecoded-read pattern. If so,
the zeros at the setup-engine offsets show nothing answering there, not
proof of a missing unit. The proof comes from the sources below.

## What the sources add

- **Family.** Linux atyfb tables 4757 as `ATI_CHIP_264GT2C`, and xf86-video-mach64
  maps `GW` to `ATI_CHIP_264GT2C`, between `264VT4` and `264GTPRO`. That is
  Rage II, not Rage Pro. The 264GT2C feature flags lack `M64F_RESET_3D`,
  which every Rage Pro entry carries.
- **No setup engine.** The DRI mach64 page lists "3D Rage II/II+/IIc"
  among chips it cannot support for want of a triangle setup engine.
  xf86-video-mach64 tags every `VERTEX_*`, `SETUP_CNTL`, `ONE_OVER_AREA`,
  `ALPHA_TST_CNTL`, `TEX_CNTL` and `SPECULAR_*` register "GTPro".
- **What it has instead.** Per RRG-G02700, 3D on this line is a trapezoid
  walk. The leading edge uses the line engine's Bresenham registers, the
  trailing edge the `TRAIL_BRES_*` set, and a write to `DST_BRES_LNTH`
  starts it. Each attribute (R, G, B, A/fog, Z, S, T) is a start value plus
  X and Y increments; S and T also take second derivatives (`*_INC2`,
  S.10.16). There is no W. The register values above show ATI's own HAL
  drives exactly this set.
- **Direct3D-visible limits** (ATI3DCIF guide SDK-C02700): 16-bit Z only;
  textures power of two up to 1024x1024. Formats 1555, 565, 8888, 332,
  4444, Y8 and YUV422, plus CI4/CI8 from Rage II on. Point, bilinear,
  mip-linear and trilinear filtering. Source blend factors are ZERO, ONE,
  DSTCLR, INVDSTCLR, SRCALPHA and INVSRCALPHA; destination factors are
  ZERO, ONE, SRCCLR, INVSRCCLR, SRCALPHA and INVSRCALPHA, with no destination
  alpha. Fog comes from the alpha interpolator and excludes alpha blending.
  No specular.
- **FIFO.** xf86-video-mach64 treats 264GT2C as VTB-or-later: `GUI_STAT` [25:16]
  is the free count. It writes `CMDFIFO_SIZE_MODE` 0 and takes the depth
  from `GUI_STAT`, which agrees with the 192 measured.
- **BAR2.** RRG-G02700 documents only BAR0 and BAR1 for VT/GT. This card
  has a live 4 KiB BAR2. Both open drivers use BAR2 when it is present, so
  the measurement disputes the 1996 document, not the drivers.

## Hypotheses this kills

- "The Rage IIC may have no BAR2 and need the in-aperture window." It has
  BAR2, and the existing `V9xPciReadAtiMmioBar` constraints (≥ 16 MiB,
  4 KiB aligned, memory) accept `DE000000` as it stands.
- "The memory decode might be the pre-VTB 3-bit table." It is the 4-bit
  table.
- "The `GUI_STAT` free-count FIFO model is Mobility-only." On the source
  evidence and the idle read it holds here. It has not been watched under
  load.

## What this means for the ati family

1. Binding 4757 for an unaccelerated desktop needs a third chip entry and
   the four exact-id checks widened: `ati_backend.c` probe,
   `ati_mach64.h`, `loader.asm`'s mini-VDD map check, and
   `eng_mach64.c`'s validate. Mode list and VRAM floor still have to come
   from this BIOS's VBE answers, which this probe does not capture.
2. The 2D fill/copy stream uses registers common to this part. One
   exception: `v9x_m64_build_2d_mode` writes `ALPHA_TST_CNTL` (`+550`),
   which is GTPro-only. On this chip that write targets nothing known. It
   should be left out for 4757 rather than assumed harmless.
3. `d3d_mach64.c` cannot serve this chip; every triangle it emits goes
   through the setup engine. `d3d_select.c` routes by engine type alone, so
   a 4757 that stamped `ATI_MACH64` to get engine fills would also get that
   back-end. Either a separate engine type or a capability check is needed
   before any engine claim.
4. Hardware Direct3D here means a new back-end: CPU triangle setup into
   trapezoids, with per-attribute gradients. That is a separate plan, and
   the first scene for it is a flat trapezoid kicked by `DST_BRES_LNTH`.

## Not established

- The VBE mode list and the BIOS's reported memory size.
- Whether the setup-engine offsets are absent or merely zero. The sources say
  absent; no write probe was run.
- FIFO depth under load, and whether 192 is a free count or a flag pattern.
- Whether the `RED_START`-style registers at `+7C0`-`+7F8` take effect when
  written. xf86 tags them GTB, while RRG-G02700 (written for the GT-A2) puts them in
  the GT set. This card's values show ATI's driver writing them.
- Whether bus-master registers (`+180`-`+1FC`) are readable without side
  effects. They were skipped.
- Why Config Manager's allocated configuration omits BAR1's I/O range.

## Sources

- Linux `drivers/video/fbdev/aty/atyfb_base.c` (`aty_chips[]`, chip feature
  sets, `atyfb_setup_generic`), `mach64_accel.c`, `mach64_ct.c`,
  `include/video/mach64.h`.
- xf86-video-mach64 `src/atichip.[ch]`, `atiregs.h`, `atiprobe.c`,
  `atipreinit.c`, `atimach64.c`, `atimach64io.c`, `atiscreen.c`.
- ATI RRG-G02700 Rev 0.10, *mach64 Register Reference Guide, ATI-264VT and
  3D RAGE* (1996), bitsavers `components/ati/`.
- ATI PRG-215R3-00-10, *RAGE PRO and Derivatives Programmer's Guide* (2000),
  bitsavers `components/ati/RAGE_PRO/`.
- ATI SDK-C02700 Rev 1.30, *3D RAGE Windows 95 Programmer's Guide*
  (ATI3DCIF), old.vgamuseum.info.
- dri.freedesktop.org wiki, ATIMach64.

No register reference specific to Rage II, II+ or IIC was found.
