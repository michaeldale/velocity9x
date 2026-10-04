/*
 * SiS 6326, PCI 1039:6326.
 *
 * Measured on two boards in A8U4I5 on 2026-10-04
 * (docs\decisions\2026-10-04-sis6326-first-survey.md):
 *
 *     revision C3, subsystem 63261039, BIOS 1.06 (1997), 4 MiB EDO
 *     revision 0B, subsystem 63261569, BIOS 1.28q (1999), 4 MiB SGRAM
 *
 * Both are AGP with a 4 MiB BAR0 framebuffer and a 64 KiB BAR1 register
 * window. The C3 board locks that machine under SiS's own driver and is
 * parked; the 0B board is the bring-up target.
 *
 * The VBE sets modes and reports the framebuffer. After every mode set the
 * enable hook turns the 2D engine's register window and the 3D accelerator
 * on, and the engine hook claims the engine for DirectDraw fill and copy and
 * for Direct3D as SIS_6326. Nothing is on
 * after a VBE mode set - SR05 locked, SRB 0Ch, SR27 00h - and the write probe
 * measured the engine working from exactly the state this hook sets
 * (docs\decisions\2026-10-05-sis6326-2d-engine-writes.md).
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, sis family only. */
extern unsigned short __far __pascal V9xPciReadSisMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniSisMmioMap(
    unsigned long bar1, unsigned long __far *linear);

static unsigned char v9x_sis_port_in(unsigned short port);
#pragma aux v9x_sis_port_in = "in al,dx" parm [dx] value [al] modify exact [al]

static void v9x_sis_port_out(unsigned short port, unsigned char value);
#pragma aux v9x_sis_port_out = "out dx,al" parm [dx] [al] modify exact []

#define V9X_SIS_SEQ_INDEX 0x03c4u
#define V9X_SIS_SEQ_DATA  0x03c5u

/* SR05 (datasheet 7.7.2): 86h unlocks the extensions and reads back A1h. */
#define V9X_SIS_SR05            0x05u
#define V9X_SIS_SR05_KEY        0x86u
#define V9X_SIS_SR05_UNLOCKED   0xa1u
/* SRB D[6:5] = 11: MMIO through PCI BAR1 (7.7.8). */
#define V9X_SIS_SRB             0x0bu
#define V9X_SIS_SRB_MMIO_MASK   0x60u
/* SR27 D7 Turbo Queue (kept off), D6 engine registers (on); D[5:0] are the
 * logical width and display start bits, left as the mode set left them
 * (7.7.38). */
#define V9X_SIS_SR27            0x27u
#define V9X_SIS_SR27_TURBO      0x80u
#define V9X_SIS_SR27_ENGINE     0x40u
/* SR39 D2: the 3D accelerator (7.7.60). The SIS3D probe ran every phase
 * with it set beside a live desktop and DirectDraw. */
#define V9X_SIS_SR39            0x39u
#define V9X_SIS_SR39_3D         0x04u

/* V9X_SIS_MMIO_BYTES in the mini-VDD. */
#define V9X_SIS_MMIO_BYTES 0x00010000ul

/* Set by the enable hook after every mode set, read by the engine hook. */
static unsigned short v9x_sis_engine_enabled = 0u;
static unsigned short v9x_sis_3d_enabled = 0u;

static unsigned char v9x_sis_sr_read(unsigned char index)
{
    v9x_sis_port_out(V9X_SIS_SEQ_INDEX, index);
    return v9x_sis_port_in(V9X_SIS_SEQ_DATA);
}

static void v9x_sis_sr_write(unsigned char index, unsigned char value)
{
    v9x_sis_port_out(V9X_SIS_SEQ_INDEX, index);
    v9x_sis_port_out(V9X_SIS_SEQ_DATA, value);
}

/*
 * The engine's register window on, after every mode set (stage 8, and the
 * DOS-box reset). Never fails Enable: a chip that does not answer the SR05
 * key, or does not keep the bits, simply gets no engine and stays on the CPU
 * path. The extensions are left unlocked, the state SiS's own driver runs in
 * (SR05 A1h, 2026-10-04 survey).
 */
static unsigned short v9x_sis6326_enable_engine(void)
{
    unsigned char srb;
    unsigned char sr27;

    v9x_sis_engine_enabled = 0u;
    v9x_sis_3d_enabled = 0u;
    v9x_sis_sr_write(V9X_SIS_SR05, V9X_SIS_SR05_KEY);
    if (v9x_sis_sr_read(V9X_SIS_SR05) != V9X_SIS_SR05_UNLOCKED) {
        return 1u;
    }

    srb = v9x_sis_sr_read(V9X_SIS_SRB);
    v9x_sis_sr_write(V9X_SIS_SRB, (unsigned char)(srb | V9X_SIS_SRB_MMIO_MASK));
    sr27 = v9x_sis_sr_read(V9X_SIS_SR27);
    v9x_sis_sr_write(V9X_SIS_SR27,
                     (unsigned char)((sr27 & ~V9X_SIS_SR27_TURBO) |
                                     V9X_SIS_SR27_ENGINE));

    if ((v9x_sis_sr_read(V9X_SIS_SRB) & V9X_SIS_SRB_MMIO_MASK) ==
            V9X_SIS_SRB_MMIO_MASK &&
        (v9x_sis_sr_read(V9X_SIS_SR27) &
         (V9X_SIS_SR27_TURBO | V9X_SIS_SR27_ENGINE)) == V9X_SIS_SR27_ENGINE) {
        v9x_sis_engine_enabled = 1u;
    }
    if (v9x_sis_engine_enabled == 0u) {
        return 1u;
    }

    /* 3D on before any 3D register access (7.7.60). SiS's own driver sets
     * it only while a Direct3D client holds a 16 bpp mode; here it stays on,
     * and the HAL creates no Direct3D context off 16 bpp. */
    v9x_sis_sr_write(V9X_SIS_SR39,
                     (unsigned char)(v9x_sis_sr_read(V9X_SIS_SR39) |
                                     V9X_SIS_SR39_3D));
    if ((v9x_sis_sr_read(V9X_SIS_SR39) & V9X_SIS_SR39_3D) != 0u) {
        v9x_sis_3d_enabled = 1u;
    }
    return 1u;
}

/*
 * BAR1 mapped by the mini-VDD, which withholds a window that reads all ones;
 * claimed only when the enable hook saw its bits hold.
 */
static void v9x_sis6326_fill_engine(unsigned long framebuffer_linear_base,
                                    unsigned long *control_linear_base,
                                    unsigned long *mapped_aperture_bytes,
                                    unsigned long *engine_type,
                                    unsigned long *engine_caps,
                                    unsigned long *gtt_linear_base,
                                    unsigned long *ring_linear_base,
                                    unsigned long *ring_bytes)
{
    unsigned long bar1 = 0ul;
    unsigned long linear = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (v9x_sis_engine_enabled == 0u) {
        return;
    }
    if (V9xPciReadSisMmioBar(&bar1) == 0u) {
        return;
    }
    if (V9xMiniSisMmioMap(bar1, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_SIS_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_SIS_6326;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
    if (v9x_sis_3d_enabled != 0u) {
        *engine_caps |= V9X_DD_ENGINE_CAP_D3D;
    }
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_sis6326_device = {
    0x1039u, 0x6326u,
    "SiS 6326",
    "1039", "6326",
    "sis-6326-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-sis6326",
    v9x_sis6326_enable_engine,
    v9x_sis6326_fill_engine
};
