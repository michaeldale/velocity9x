/*
 * The DirectDraw glue's interface to the rest of the 16-bit driver.
 *
 * The implementation is src\display16\dd16.c. A family with no DirectDraw HAL
 * - matrox-m2 today - links no-op forms of the two driver-object entries from
 * the same file, so every caller here is unconditional.
 *
 * Include <windows.h> before this header: the types below are the Win16 ones,
 * the same convention gdi_accel.h uses.
 */
#ifndef VELOCITY9X_DD16_H
#define VELOCITY9X_DD16_H

/* Build or refresh the DDRAW driver object. Non-zero on success; zero also
 * means "DriverInit has not run yet", which DDRAW retries. */
extern WORD FAR PASCAL V9xDdCreateDriverObject(WORD reset);

/* Mark the framebuffer and engine descriptors stale, on Disable and before a
 * mode switch. */
extern void FAR PASCAL V9xDdInvalidate(void);

/*
 * Resolve [Velocity9x] Direct3D from SYSTEM.INI against this chip's engine
 * descriptor. Called once per Enable, from v9x_build_pdevice, beside
 * v9x_gdi_accel_configure() and for the same reason: the mode and the chip
 * are both settled by then, and a ReEnable comes back through it.
 *
 * The result is applied where the engine capabilities are stamped into the
 * shared block, which is before DDRAW ever reads them.
 */
void v9x_dd_d3d_configure(void);

/*
 * The resolved state, for the Direct3DMode= diagnostics key. Returns a static
 * string; never null. Available on a family with no DirectDraw HAL too, where
 * it reports the card's answer rather than nothing at all.
 */
const char *v9x_dd_d3d_state_text(void);

#endif /* VELOCITY9X_DD16_H */
