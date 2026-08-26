/*
 * GDI acceleration: the interface the rest of the 16-bit driver needs.
 *
 * The implementation is src\display16\gdi_accel.c, which links into every
 * family. Three of the four have no 2D engine at all - ati, vbe and matrox-m2
 * all declare EngineType NONE on every chip - so for three quarters of the
 * fleet the decline path in that file is not scaffolding, it is the shipping
 * code, permanently. Everything here is written with that in mind: nothing
 * costs a family without an engine more than one flag test.
 *
 * See docs\plans\gdi-acceleration.md and
 * docs\plans\gdi-accel-000-and-harness.md.
 */
#ifndef VELOCITY9X_GDI_ACCEL_H
#define VELOCITY9X_GDI_ACCEL_H

/*
 * Pending engine work that the CPU must not cross.
 *
 * Read by the two deBeginAccess entries in runtime.asm as their fast-path
 * test, so it is a WORD and it lives in DGROUP. Non-zero means the engine has
 * been given a command that may not have completed.
 */
extern WORD v9x_gdi_engine_dirty;

/*
 * Session-long poison latch. Set when a bounded engine wait expires, and
 * never cleared except by a reboot: every later operation declines at the
 * first gate, so the desktop keeps rendering through the DIB Engine.
 *
 * In DGROUP rather than in the PDEVICE because ReEnable rebuilds the PDEVICE
 * in place on a live mode switch, and the latch is required to survive one.
 */
extern WORD v9x_gdi_poisoned;

/* Read the SYSTEM.INI keys and clamp them by what this chip's engine can do.
 * Called once per Enable, from v9x_build_pdevice. */
void v9x_gdi_accel_configure(void);

/* Flush a deferred poison report. The bounded waits run at interrupt time on
 * a software-cursor draw, where WritePrivateProfileString and the serial port
 * are both forbidden, so reporting is deferred to whichever comes first: the
 * next BitBlt, or Disable. */
void v9x_gdi_accel_flush_report(void);

/*
 * Effective acceleration state, for the Acceleration= diagnostics key.
 * Returns a static string; never null.
 */
const char *v9x_gdi_accel_state_text(void);

/* V9X_GDIGETSTATS. Non-zero when the block was filled in. */
WORD v9x_gdi_accel_stats(void FAR *output);

/* V9X_GDIFAULTINJECT. Arms `count` forced bounded-wait timeouts. */
WORD v9x_gdi_accel_fault_inject(DWORD count);

#endif /* VELOCITY9X_GDI_ACCEL_H */
