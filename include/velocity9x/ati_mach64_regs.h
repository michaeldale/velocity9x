/* Project-owned ATI Mach64 register subset used by the shared engine layer. */
#ifndef VELOCITY9X_ATI_MACH64_REGS_H
#define VELOCITY9X_ATI_MACH64_REGS_H

/* BAR2-relative addresses. Block 0 begins at +0x400. */
#define V9X_M64_GUI_CNTL              0x00000178ul
#define V9X_M64_FIFO_STAT             0x00000310ul
#define V9X_M64_GUI_TRAJ_CNTL         0x00000330ul
#define V9X_M64_GUI_STAT              0x00000338ul
#define V9X_M64_MEM_BUF_CNTL          0x0000042cul
#define V9X_M64_BUS_CNTL              0x000004a0ul
#define V9X_M64_GEN_TEST_CNTL         0x000004d0ul
#define V9X_M64_CONFIG_CHIP_ID        0x000004e0ul

#define V9X_M64_FIFO_ERR              0x80000000ul
#define V9X_M64_GUI_ACTIVE            0x00000001ul
#define V9X_M64_GUI_FIFO_MASK         0x03ff0000ul
#define V9X_M64_GUI_FIFO_SHIFT        16u

#define V9X_M64_INVALIDATE_RB_CACHE   0x00800000ul
#define V9X_M64_BUS_FLUSH_BUF         0x00000004ul
#define V9X_M64_BUS_HOST_ERR_INT_EN   0x00400000ul
#define V9X_M64_BUS_HOST_ERR_INT      0x00800000ul
#define V9X_M64_GEN_GUI_RESETB        0x00000100ul

#define V9X_M64_VT_FIFO_ENTRIES       16ul
#define V9X_M64_SHADOW_ENTRIES        32ul

#endif
