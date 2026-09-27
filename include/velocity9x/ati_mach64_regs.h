/* Project-owned ATI Mach64 register subset used by the shared engine layer. */
#ifndef VELOCITY9X_ATI_MACH64_REGS_H
#define VELOCITY9X_ATI_MACH64_REGS_H

/* BAR2-relative addresses. Block 0 begins at +0x400. */
#define V9X_M64_GUI_CNTL              0x00000178ul
#define V9X_M64_DST_OFF_PITCH         0x00000500ul
#define V9X_M64_DST_Y_X               0x0000050cul
#define V9X_M64_DST_HEIGHT_WIDTH      0x00000518ul
#define V9X_M64_DST_CNTL              0x00000530ul
#define V9X_M64_Z_OFF_PITCH           0x00000548ul
#define V9X_M64_Z_CNTL                0x0000054cul
#define V9X_M64_ALPHA_TST_CNTL        0x00000550ul
#define V9X_M64_SRC_OFF_PITCH         0x00000580ul
#define V9X_M64_SRC_Y_X               0x0000058cul
#define V9X_M64_SRC_WIDTH1            0x00000590ul
#define V9X_M64_SCALE_3D_CNTL         0x000005fcul
#define V9X_M64_SC_LEFT_RIGHT         0x000006a8ul
#define V9X_M64_SC_TOP_BOTTOM         0x000006b4ul
#define V9X_M64_DP_FRGD_CLR           0x000006c4ul
#define V9X_M64_DP_WRITE_MASK         0x000006c8ul
#define V9X_M64_DP_PIX_WIDTH          0x000006d0ul
#define V9X_M64_DP_MIX                0x000006d4ul
#define V9X_M64_DP_SRC                0x000006d8ul
#define V9X_M64_CLR_CMP_CNTL          0x00000708ul
#define V9X_M64_TEX_SIZE_PITCH        0x00000770ul
#define V9X_M64_TEX_CNTL              0x00000774ul
#define V9X_M64_FIFO_STAT             0x00000310ul
#define V9X_M64_GUI_TRAJ_CNTL         0x00000330ul
#define V9X_M64_GUI_STAT              0x00000338ul
#define V9X_M64_MEM_BUF_CNTL          0x0000042cul
#define V9X_M64_BUS_CNTL              0x000004a0ul
#define V9X_M64_GEN_TEST_CNTL         0x000004d0ul
#define V9X_M64_CONFIG_CHIP_ID        0x000004e0ul
/* BAR2-relative block-1 setup registers occupy the lower 1 KiB. */
#define V9X_M64_VERTEX_1_S            0x00000240ul
#define V9X_M64_VERTEX_1_T            0x00000244ul
#define V9X_M64_VERTEX_1_W            0x00000248ul
#define V9X_M64_VERTEX_1_Z            0x00000250ul
#define V9X_M64_VERTEX_1_ARGB         0x00000254ul
#define V9X_M64_VERTEX_1_X_Y          0x00000258ul
#define V9X_M64_ONE_OVER_AREA         0x0000029cul
#define V9X_M64_VERTEX_2_S            0x00000260ul
#define V9X_M64_VERTEX_2_T            0x00000264ul
#define V9X_M64_VERTEX_2_W            0x00000268ul
#define V9X_M64_VERTEX_2_Z            0x00000270ul
#define V9X_M64_VERTEX_2_ARGB         0x00000274ul
#define V9X_M64_VERTEX_2_X_Y          0x00000278ul
#define V9X_M64_VERTEX_3_S            0x00000280ul
#define V9X_M64_VERTEX_3_T            0x00000284ul
#define V9X_M64_VERTEX_3_W            0x00000288ul
#define V9X_M64_VERTEX_3_Z            0x00000290ul
#define V9X_M64_VERTEX_3_ARGB         0x00000294ul
#define V9X_M64_VERTEX_3_X_Y          0x00000298ul
#define V9X_M64_SETUP_CNTL            0x00000304ul

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
#define V9X_M64_FILL_REPAIR_DWORDS    3ul
#define V9X_M64_COPY_DWORDS           14ul
#define V9X_M64_FLAT_TRIANGLE_DWORDS  19ul
#define V9X_M64_FLAT_STATE_DWORDS     17ul

#define V9X_M64_DST_X_DIR             0x00000001ul
#define V9X_M64_DST_Y_DIR             0x00000002ul

#endif
