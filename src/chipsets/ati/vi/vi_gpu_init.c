/*
 * The Carrizo engine configuration list; see include\velocity9x\vi_gpu_init.h.
 */
#include "velocity9x/vi_gpu_init.h"

#define VI_WRITE                    0xfffffffful
/* gfx_v8_0_gpu_init's values for Carrizo. */
#define VI_GRBM_READ_TIMEOUT_MASK   0x000000fful
#define VI_GB_ADDR_CONFIG_GOLDEN    0x22010001ul
#define VI_VMIDS                    16ul
#define VI_SRBM_VMID_SHIFT          4
/* SH_MEM_CONFIG: ALIGNMENT_MODE 4:3 unaligned (3), DEFAULT_MTYPE 7:5 and
 * APE1_MTYPE 10:8 UC (3) for VMID 0, NC (1) for the rest. */
#define VI_SH_MEM_CONFIG_VMID0      0x00000378ul
#define VI_SH_MEM_CONFIG_OTHERS     0x00000138ul
#define VI_SH_MEM_APE1_BASE         1ul
#define VI_GRBM_BROADCAST           0xe0000000ul
/* PA_SC_FIFO_SIZE: Carrizo's sc_prim_fifo_size_frontend 20h (5:0),
 * _backend 100h (14:6), sc_hiz_tile_fifo_size 30h (20:15),
 * sc_earlyz_tile_fifo_size 130h (31:23). */
#define VI_PA_SC_FIFO_SIZE          ((0x20ul << 0) | (0x100ul << 6) | \
                                     (0x30ul << 15) | (0x130ul << 23))

struct v9x_vi_ops_out {
    struct v9x_vi_reg_seq *ops;
    v9x_u32 count;
    v9x_u32 capacity;
};

static void v9x_vi_op(struct v9x_vi_ops_out *out, v9x_u32 offset,
                      v9x_u32 clear, v9x_u32 set)
{
    if (out->count < out->capacity) {
        out->ops[out->count].offset = offset;
        out->ops[out->count].clear = clear;
        out->ops[out->count].set = set;
    }
    ++out->count;
}

static void v9x_vi_sequence(struct v9x_vi_ops_out *out,
                            const struct v9x_vi_reg_seq *table, v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        v9x_vi_op(out, table[index].offset, table[index].clear,
                  table[index].set);
    }
}

/* tiling_mode_table_init's Carrizo loops leave these entries as the BIOS
 * left them. */
static int v9x_vi_tile_skipped(v9x_u32 index)
{
    return index == 7ul || index == 12ul || index == 17ul || index == 23ul;
}

v9x_u32 v9x_vi_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity)
{
    struct v9x_vi_ops_out out;
    v9x_u32 index;
    v9x_u32 vmid;

    if (ops == 0) {
        return 0ul;
    }
    out.ops = ops;
    out.count = 0ul;
    out.capacity = capacity;

    /* gfx_v8_0_init_golden_registers, CHIP_CARRIZO. */
    v9x_vi_sequence(&out, v9x_vi_cz_mgcg_cgcg_init,
                    V9X_VI_CZ_MGCG_CGCG_COUNT);
    v9x_vi_sequence(&out, v9x_vi_cz_golden_settings, V9X_VI_CZ_GOLDEN_COUNT);
    v9x_vi_sequence(&out, v9x_vi_cz_golden_common,
                    V9X_VI_CZ_GOLDEN_COMMON_COUNT);

    /* gfx_v8_0_gpu_init. */
    v9x_vi_op(&out, V9X_VI_GRBM_CNTL, VI_GRBM_READ_TIMEOUT_MASK,
              VI_GRBM_READ_TIMEOUT_MASK);
    v9x_vi_op(&out, V9X_VI_GB_ADDR_CONFIG, VI_WRITE, VI_GB_ADDR_CONFIG_GOLDEN);
    v9x_vi_op(&out, V9X_VI_HDP_ADDR_CONFIG, VI_WRITE,
              VI_GB_ADDR_CONFIG_GOLDEN);
    for (index = 0ul; index < V9X_VI_TILE_MODES; ++index) {
        if (!v9x_vi_tile_skipped(index)) {
            v9x_vi_op(&out, V9X_VI_GB_TILE_MODE0 + index * 4ul, VI_WRITE,
                      v9x_vi_cz_tile_modes[index]);
        }
    }
    for (index = 0ul; index < V9X_VI_MACROTILE_MODES; ++index) {
        if (index != 7ul) {
            v9x_vi_op(&out, V9X_VI_GB_MACROTILE_MODE0 + index * 4ul, VI_WRITE,
                      v9x_vi_cz_macrotile_modes[index]);
        }
    }
    for (vmid = 0ul; vmid < VI_VMIDS; ++vmid) {
        v9x_vi_op(&out, V9X_VI_SRBM_GFX_CNTL, VI_WRITE,
                  vmid << VI_SRBM_VMID_SHIFT);
        v9x_vi_op(&out, V9X_VI_SH_MEM_CONFIG, VI_WRITE,
                  vmid == 0ul ? VI_SH_MEM_CONFIG_VMID0 :
                                VI_SH_MEM_CONFIG_OTHERS);
        v9x_vi_op(&out, V9X_VI_SH_MEM_APE1_BASE, VI_WRITE,
                  VI_SH_MEM_APE1_BASE);
        v9x_vi_op(&out, V9X_VI_SH_MEM_APE1_LIMIT, VI_WRITE, 0ul);
        v9x_vi_op(&out, V9X_VI_SH_MEM_BASES, VI_WRITE, 0ul);
    }
    v9x_vi_op(&out, V9X_VI_SRBM_GFX_CNTL, VI_WRITE, 0ul);
    v9x_vi_op(&out, V9X_VI_GRBM_GFX_INDEX, VI_WRITE, VI_GRBM_BROADCAST);
    v9x_vi_op(&out, V9X_VI_PA_SC_FIFO_SIZE, VI_WRITE, VI_PA_SC_FIFO_SIZE);

    return out.count <= capacity ? out.count : 0ul;
}
