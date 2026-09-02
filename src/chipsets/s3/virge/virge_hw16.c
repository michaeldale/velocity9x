/*
 * S3 ViRGE/DX 86C375 chip module.
 *
 * The chip data that used to be #ifdef'd into src\display16\ddi.c: the PCI
 * identity, the strings published to C:\V9XDIAG\V9XHW.INI, and the two hooks that
 * differ from its Trio64 sibling. Register access is shared with the other S3
 * chips in src\chipsets\s3\common\s3_regs16.c, and the mode table and the rest
 * of the family description live in src\chipsets\s3\s3_hw16.c.
 *
 * This object holds ViRGE code and no Trio64 code. The per-object audit layer
 * asserts exactly that, which is what keeps one S3 binary from becoming a
 * place where the two chips' register sequences quietly merge.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

/*
 * The ViRGE adds CR53[3] on top of the shared S3 sequence: it opens the "new
 * MMIO" window, without which offsets such as SUBSYS_STAT (8504h) address
 * framebuffer memory rather than engine registers.
 *
 * This lives here rather than in s3_regs16.c on purpose. The Trio has no such
 * window, and a shared implementation would put the CR53 signature into code
 * the Trio64 also runs and defeat the per-object audit.
 */
static unsigned short v9x_virge_enable_aperture(void)
{
    unsigned char value;

    if (v9x_s3_enable_linear_aperture() == 0u) {
        return 0u;
    }
    value = v9x_s3_crtc_read(0x53u);
    v9x_s3_crtc_write(0x53u, (unsigned char)(value | 0x08u));
    if ((v9x_s3_crtc_read(0x53u) & 0x08u) == 0u) {
        return 0u;
    }
    return 1u;
}

/*
 * ViRGE/DX engine: the S3D core plus the new-MMIO window at BAR + 16 MiB.
 * V9xHardwareEnable maps the whole 64 MiB aperture, and register offsets such
 * as SUBSYS_STAT (8504h) are relative to that 64 KiB window, not to VRAM.
 */
static void v9x_virge_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps)
{
    *control_linear_base = framebuffer_linear_base + 0x01000000ul;
    *mapped_aperture_bytes = 0x00010000ul;
    *engine_type = V9X_DD_ENGINE_TYPE_S3_VIRGE_DX;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VBLANK |
                   V9X_DD_ENGINE_CAP_D3D;
}

/* Not static: the family table points at it, and the link map is where the
 * per-chip audit looks for it, now that the PCI identity is data in this
 * object rather than an immediate in the assembled runtime. */
const V9X_HW16_DEVICE v9x_virge_device = {
    0x5333u, 0x8a01u,
    "S3 ViRGE/DX 86C375",
    "5333", "8A01",
    "s3-virge-pll-v1",
    "live-any-depth",
    "directdraw-fill-blt",
    "hardware-s3d",
    v9x_virge_enable_aperture,
    v9x_virge_fill_engine
};

/*
 * The Trio3D/2X, driven by the ViRGE's hooks.
 *
 * It sits in this object rather than the Trio64's deliberately, and the name
 * is the reason it is easy to put in the wrong one: despite "Trio" it is an
 * S3D part, not a Trio64. It opens the same CR53[3] new-MMIO window and
 * describes the same S3D engine, so it gets hardware Direct3D where the
 * Trio64 aliases get none.
 *
 * The evidence is 86Box's, not silicon's: that emulator implements this chip
 * inside its ViRGE driver, in the same chip enum, writing this exact id - and
 * its S3D triangle engine carries no branch for the part. Two modelled
 * differences are unaccounted for here and are the first things to suspect if
 * it misbehaves: a 16-slot command FIFO where the ViRGE/DX has 8, and an
 * 8 MiB decode mask where the ViRGE/DX has 4 MiB. See
 * docs\decisions\2026-09-02-trio3d-on-the-s3-path.md.
 */
const V9X_HW16_DEVICE v9x_trio3d2x_device = {
    0x5333u, 0x8a13u,
    "S3 Trio3D/2X",
    "5333", "8A13",
    "s3-virge-pll-v1",
    "live-any-depth",
    "directdraw-fill-blt",
    "hardware-s3d",
    v9x_virge_enable_aperture,
    v9x_virge_fill_engine
};
