/*
 * The screen-switch protocol of every Windows 98 DDK display sample
 * (98DDK\src\display\mini\framebuf\SSWITCH.ASM), called by runtime.asm's
 * INT 2Fh hook.
 *
 * AX=4001h: a full-screen DOS box is taking the display, so the DIB Engine
 * must stop drawing to it. AX=4002h: Windows has it back. The VDD has
 * normally called ResetHiResMode by then, which cleared the busy flag; if it
 * has not, the mode is reset here, as the samples do. Then USER repaints
 * every window. Without this a full-screen round trip left the desktop
 * frozen and unrepainted (docs\decisions\2026-11-10-dos-box-return-screen-
 * switch-hook.md).
 *
 * Compiled into its own code segment, SSWCODE, with the hook's assembly, to
 * keep _TEXT under budget. Every call out of this unit is to a FAR PASCAL
 * entry in ddi.c, so nothing here makes a near call across the boundary.
 */
/* The display ABI header is this unit's one OS boundary; it brings in
 * <windows.h>. */
#include "win9x_display_abi.h"

#define V9X_SCREEN_SWITCH_OUT 0x4001u

extern V9X_DIB_ENGINE FAR *v9x_driver_pdevice;
extern void __loadds FAR PASCAL ResetHiResMode(void);
extern void FAR PASCAL V9xRequestRepaint(void);
void __loadds FAR PASCAL V9xScreenSwitch(WORD function);

void __loadds FAR PASCAL V9xScreenSwitch(WORD function)
{
    if (v9x_driver_pdevice == 0) {
        return;
    }
    if (function == V9X_SCREEN_SWITCH_OUT) {
        v9x_driver_pdevice->deFlags |= V9X_DE_BUSY;
        return;
    }
    if ((v9x_driver_pdevice->deFlags & V9X_DE_BUSY) != 0u) {
        ResetHiResMode();
    }
    V9xRequestRepaint();
}
