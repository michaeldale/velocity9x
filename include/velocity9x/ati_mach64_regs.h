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
#define V9X_M64_TEX_0_OFF             0x000005c0ul
#define V9X_M64_SCALE_3D_CNTL         0x000005fcul
#define V9X_M64_SC_LEFT_RIGHT         0x000006a8ul
#define V9X_M64_SC_TOP_BOTTOM         0x000006b4ul
#define V9X_M64_DP_FRGD_CLR           0x000006c4ul
/* The same register: the 3D engine's fog colour (Mesa mach64_reg.h). */
#define V9X_M64_DP_FOG_CLR            V9X_M64_DP_FRGD_CLR
/* Specular ARGB; its alpha is the fog factor (Phase 4 item 12). */
#define V9X_M64_VERTEX_1_SPEC_ARGB    0x0000024cul
#define V9X_M64_VERTEX_2_SPEC_ARGB    0x0000026cul
#define V9X_M64_VERTEX_3_SPEC_ARGB    0x0000028cul
#define V9X_M64_DP_WRITE_MASK         0x000006c8ul
#define V9X_M64_DP_PIX_WIDTH          0x000006d0ul
#define V9X_M64_DP_MIX                0x000006d4ul
#define V9X_M64_DP_SRC                0x000006d8ul
#define V9X_M64_CLR_CMP_CNTL          0x00000708ul
#define V9X_M64_TEX_SIZE_PITCH        0x00000770ul
#define V9X_M64_TEX_CNTL              0x00000774ul
#define V9X_M64_SECONDARY_TEX_OFF     0x00000778ul
/* Block 0 like their neighbours: X.Org indices C4h, CCh and CEh, so
 * 0x400 + 4 * index. Until 2026-09-28 these three lacked the 0x400 and
 * named block 1 locations instead; the diagnostic VxD always used 0738h
 * and 0730h, so only the HAL's first physical draw read the wrong status
 * word, over-reserved the FIFO, and hung the Gateway. */
#define V9X_M64_FIFO_STAT             0x00000710ul
#define V9X_M64_GUI_TRAJ_CNTL         0x00000730ul
#define V9X_M64_GUI_STAT              0x00000738ul
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
/* A7 alias follows vertex 3; 97/9F are equivalent, C0 is the DMA/UC alias. */
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
#define V9X_M64_SETUP_GOURAUD         0x00000000ul
#define V9X_M64_SETUP_FLAT_VERTEX_3   0x00000018ul

#define V9X_M64_Z_ENABLE              0x00000001ul
#define V9X_M64_Z_TEST_NEVER          0x00000000ul
#define V9X_M64_Z_TEST_LESS           0x00000010ul
#define V9X_M64_Z_TEST_LESSEQUAL      0x00000020ul
#define V9X_M64_Z_TEST_EQUAL          0x00000030ul
#define V9X_M64_Z_TEST_GREATEREQUAL   0x00000040ul
#define V9X_M64_Z_TEST_GREATER        0x00000050ul
#define V9X_M64_Z_TEST_NOTEQUAL       0x00000060ul
#define V9X_M64_Z_TEST_ALWAYS         0x00000070ul
#define V9X_M64_Z_WRITE_ENABLE        0x00000100ul

#define V9X_M64_FIFO_ERR              0x80000000ul
#define V9X_M64_GUI_ACTIVE            0x00000001ul
#define V9X_M64_GUI_FIFO_MASK         0x03ff0000ul
#define V9X_M64_GUI_FIFO_SHIFT        16u

#define V9X_M64_INVALIDATE_RB_CACHE   0x00800000ul
#define V9X_M64_BUS_FLUSH_BUF         0x00000004ul
/* Enables register block 1, which holds the setup engine (VERTEX_*,
 * ONE_OVER_AREA, SETUP_CNTL). X.Org sets it for every chip from 264VT
 * (atilock.c, atimach64.c); ATI's driver ran with it set, and a bare VBE
 * boot leaves it clear (BUS_CNTL 7B33A001 vs 7333A001 on the Gateway). */
#define V9X_M64_BUS_EXT_REG_EN        0x08000000ul
#define V9X_M64_BUS_HOST_ERR_INT_EN   0x00400000ul
#define V9X_M64_BUS_HOST_ERR_INT      0x00800000ul
#define V9X_M64_GEN_GUI_RESETB        0x00000100ul

#define V9X_M64_VT_FIFO_ENTRIES       16ul
#define V9X_M64_SHADOW_ENTRIES        32ul
#define V9X_M64_FILL_DWORDS           12ul
#define V9X_M64_FILL_REPAIR_DWORDS    3ul
#define V9X_M64_COPY_DWORDS           14ul
#define V9X_M64_FLAT_TRIANGLE_DWORDS  19ul
#define V9X_M64_GOURAUD_TRIANGLE_DWORDS V9X_M64_FLAT_TRIANGLE_DWORDS
#define V9X_M64_FLAT_STATE_DWORDS     17ul
#define V9X_M64_GOURAUD_STATE_DWORDS  V9X_M64_FLAT_STATE_DWORDS
#define V9X_M64_TEXTURED_STATE_DWORDS 19ul
#define V9X_M64_TEXTURED_TRIANGLE_DWORDS V9X_M64_FLAT_TRIANGLE_DWORDS
/* A draw's state is the textured state's slots at most; a setup packet is
 * a triangle plus the three specular words fog needs ahead of it. */
#define V9X_M64_DRAW_STATE_DWORDS     V9X_M64_TEXTURED_STATE_DWORDS
#define V9X_M64_SPECULAR_DWORDS       3ul
#define V9X_M64_SETUP_DWORDS \
    (V9X_M64_FLAT_TRIANGLE_DWORDS + V9X_M64_SPECULAR_DWORDS)

#define V9X_M64_SCALE_3D_FCN_TEXTURE  0x00000080ul
#define V9X_M64_SCALE_3D_TEXTURE_RGB565 0x40000000ul
#define V9X_M64_SCALE_3D_TEXTURE_ARGB1555 0x30000000ul
#define V9X_M64_SCALE_3D_TEXTURE_ARGB4444 0xf0000000ul
#define V9X_M64_TEX_MAP_AEN           0x40000000ul
/* SCALE_3D_CNTL TEX_LIGHT_FCN [23:22], measured in Phase 4 item 10. */
#define V9X_M64_TEX_LIGHT_FCN_MASK    0x00c00000ul
#define V9X_M64_TEX_LIGHT_FCN_REPLACE 0x00000000ul
#define V9X_M64_TEX_LIGHT_FCN_MODULATE 0x00400000ul
#define V9X_M64_TEX_LIGHT_FCN_ALPHA_DECAL 0x00800000ul
/* SCALE_3D_CNTL ALPHA_FOG_EN [12:11] = 2: the blend fields then mix with
 * DP_FOG_CLR by specular alpha (Phase 4 item 12). */
#define V9X_M64_ALPHA_FOG_EN_FOG      0x00001000ul
#define V9X_M64_BILINEAR_TEX_EN       0x02000000ul
#define V9X_M64_TEX_BLEND_FCN_LINEAR  0x08000000ul
#define V9X_M64_TEXTURE_CLAMP_S       0x00020000ul
#define V9X_M64_TEXTURE_CLAMP_T       0x00040000ul
#define V9X_M64_TEX_CACHE_FLUSH       0x00800000ul
#define V9X_M64_TEX_CACHE_SIZE_4K     0x40000000ul

#define V9X_M64_ALPHA_BLEND_ENABLE       0x00000800ul
#define V9X_M64_ALPHA_BLEND_SATURATE     0x00002000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_MASK  0x00070000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_ZERO  0x00000000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_ONE   0x00010000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_DST_COLOR 0x00020000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_INV_DST_COLOR 0x00030000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_SRC_ALPHA 0x00040000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_INV_SRC_ALPHA 0x00050000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_DST_ALPHA 0x00060000ul
#define V9X_M64_ALPHA_BLEND_SOURCE_INV_DST_ALPHA 0x00070000ul
#define V9X_M64_ALPHA_BLEND_DEST_MASK    0x00380000ul
#define V9X_M64_ALPHA_BLEND_DEST_ZERO    0x00000000ul
#define V9X_M64_ALPHA_BLEND_DEST_ONE     0x00080000ul
#define V9X_M64_ALPHA_BLEND_DEST_SRC_COLOR 0x00100000ul
#define V9X_M64_ALPHA_BLEND_DEST_INV_SRC_COLOR 0x00180000ul
#define V9X_M64_ALPHA_BLEND_DEST_SRC_ALPHA 0x00200000ul
#define V9X_M64_ALPHA_BLEND_DEST_INV_SRC_ALPHA 0x00280000ul
#define V9X_M64_ALPHA_BLEND_DEST_DST_ALPHA 0x00300000ul
#define V9X_M64_ALPHA_BLEND_DEST_INV_DST_ALPHA 0x00380000ul

#define V9X_M64_ALPHA_TEST_ENABLE       0x00000001ul
#define V9X_M64_ALPHA_TEST_NEVER        0x00000000ul
#define V9X_M64_ALPHA_TEST_LESS         0x00000010ul
#define V9X_M64_ALPHA_TEST_LESSEQUAL    0x00000020ul
#define V9X_M64_ALPHA_TEST_EQUAL        0x00000030ul
#define V9X_M64_ALPHA_TEST_GREATEREQUAL 0x00000040ul
#define V9X_M64_ALPHA_TEST_GREATER      0x00000050ul
#define V9X_M64_ALPHA_TEST_NOTEQUAL     0x00000060ul
#define V9X_M64_ALPHA_TEST_ALWAYS       0x00000070ul
#define V9X_M64_ALPHA_TEST_SOURCE_VERTEX 0x00001000ul
#define V9X_M64_ALPHA_REFERENCE_SHIFT   16ul

#define V9X_M64_TEXTURE_FORMAT_RGB565   0ul
#define V9X_M64_TEXTURE_FORMAT_ARGB1555 1ul
#define V9X_M64_TEXTURE_FORMAT_ARGB4444 2ul

#define V9X_M64_DST_X_DIR             0x00000001ul
#define V9X_M64_DST_Y_DIR             0x00000002ul

#endif
