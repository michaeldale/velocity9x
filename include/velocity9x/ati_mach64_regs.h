/* Project-owned ATI Mach64 register subset used by the shared engine layer. */
#ifndef VELOCITY9X_ATI_MACH64_REGS_H
#define VELOCITY9X_ATI_MACH64_REGS_H

/* BAR2-relative addresses. Block 0 begins at +0x400. */
#define V9X_M64_GUI_CNTL              0x00000178ul
#define V9X_M64_DST_OFF_PITCH         0x00000500ul
#define V9X_M64_DST_Y_X               0x0000050cul
#define V9X_M64_DST_HEIGHT_WIDTH      0x00000518ul
#define V9X_M64_DST_CNTL              0x00000530ul
#define V9X_M64_SC_LEFT_RIGHT         0x000006a8ul
#define V9X_M64_SC_TOP_BOTTOM         0x000006b4ul
#define V9X_M64_DP_FRGD_CLR           0x000006c4ul
#define V9X_M64_DP_WRITE_MASK         0x000006c8ul
#define V9X_M64_DP_PIX_WIDTH          0x000006d0ul
#define V9X_M64_DP_MIX                0x000006d4ul
#define V9X_M64_DP_SRC                0x000006d8ul
#define V9X_M64_CLR_CMP_CNTL          0x00000708ul
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
#define V9X_M64_FILL_DWORDS           12ul

#endif
