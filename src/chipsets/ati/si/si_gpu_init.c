/*
 * The Southern Islands engine configuration list; see
 * include\velocity9x\si_gpu_init.h.
 *
 * Linux's gfx_v6_0_constants_init, after si_init_golden_registers, for
 * CHIP_OLAND, CHIP_VERDE, CHIP_PITCAIRN and CHIP_TAHITI. Offsets are
 * gfx_6_0_d.h, oss_1_0_d.h and gmc_6_0_d.h dword indices times four; field
 * positions gfx_6_0_sh_mask.h; each chip's numbers constants_init's case
 * for it.
 *
 * Linux decides five things on what it reads: the DRAM row, from
 * MC_ARB_RAMCFG; the raster configuration, from the RBs each shader engine
 * and array has (setup_rb); which CU of each array SPI_STATIC_THREAD_MGMT_3
 * leaves out (setup_spi); and the texture channel steering when a TCC is
 * off (setup_tcc). Here they are made from struct v9x_si_gpu_config, so
 * the per-engine and per-array loops Linux runs for two-engine chips are
 * host-tested: each GRBM_GFX_INDEX select Linux writes becomes an entry,
 * and each register it writes under it another.
 *
 * Left out, as Mullins' list leaves its equivalents out: SRBM_INT_CNTL and
 * _ACK (interrupts), BIF_FB_EN (the BIOS leaves 3, read and write on),
 * DMIF_ADDR_CONFIG and DMIF_ADDR_CALC (display), the reads Linux writes
 * straight back (SX_DEBUG_1, HDP_HOST_PATH_CNTL), and setup_rb's second
 * pass, which only reads.
 */
#include "velocity9x/si_gpu_init.h"

#define SI_WRITE                    0xfffffffful

#define SI_GRBM_CNTL                0x08000ul
#define SI_GB_ADDR_CONFIG           0x098f8ul
#define SI_HDP_ADDR_CONFIG          0x02f48ul
#define SI_DMA0_TILING_CONFIG       0x0d0b8ul
#define SI_DMA1_TILING_CONFIG       0x0d8b8ul
#define SI_GB_TILE_MODE0            0x09910ul
#define SI_PA_SC_RASTER_CONFIG      0x28350ul
#define SI_SPI_STATIC_THREAD_MGMT_3 0x090e8ul
#define SI_TCP_CHAN_STEER_LO        0x0ac0cul
#define SI_TCP_CHAN_STEER_HI        0x0ac10ul
#define SI_TCP_ADDR_CONFIG          0x0ac14ul
#define SI_CP_QUEUE_THRESHOLDS      0x08760ul
#define SI_CP_MEQ_THRESHOLDS        0x08764ul
#define SI_SPI_CONFIG_CNTL_1        0x0913cul
#define SI_PA_SC_FIFO_SIZE          0x08bccul
#define SI_VGT_NUM_INSTANCES        0x08974ul
#define SI_CP_PERFMON_CNTL          0x087fcul
#define SI_SQ_CONFIG                0x08c00ul
#define SI_PA_SC_FORCE_EOV_MAX      0x08b24ul
#define SI_VGT_CACHE_INVALIDATION   0x088c4ul
#define SI_VGT_GS_VERTEX_REUSE      0x088d4ul
#define SI_PA_SC_LINE_STIPPLE_STATE 0x08b10ul
#define SI_CB_PERFCOUNTER0_SELECT0  0x09a20ul
#define SI_CB_PERFCOUNTER_SELECTS   8ul
#define SI_PA_CL_ENHANCE            0x08a14ul

/* GRBM_CNTL READ_TIMEOUT 7:0. */
#define SI_GRBM_READ_TIMEOUT        0x000000fful
/* The golden GB_ADDR_CONFIGs constants_init starts from:
 * TAHITI_GB_ADDR_CONFIG_GOLDEN for Tahiti and Pitcairn,
 * VERDE_GB_ADDR_CONFIG_GOLDEN for Cape Verde and the Oland. ROW_SIZE
 * (29:28) and NUM_SHADER_ENGINES (13:12) are then replaced. */
#define SI_GB_ADDR_TAHITI_GOLDEN    0x12011003ul
#define SI_GB_ADDR_VERDE_GOLDEN     0x12010002ul
#define SI_GB_ADDR_ROW_SIZE_MASK    0x30000000ul
#define SI_GB_ADDR_ROW_SIZE_SHIFT   28u
#define SI_GB_ADDR_NUM_SE_MASK      0x00003000ul
#define SI_GB_ADDR_NUM_SE_SHIFT     12u
/* MC_ARB_RAMCFG NOOFCOLS (7:6): the row is 4 * 2^(8 + NOOFCOLS) bytes,
 * at most 4 KiB as constants_init clamps it. */
#define SI_RAMCFG_NOOFCOLS_MASK     0x000000c0ul
#define SI_RAMCFG_NOOFCOLS_SHIFT    6u
#define SI_ROW_KIB_MAX              4ul
/* GB_TILE_MODE TILE_SPLIT (13:11): ADDR_SURF_TILE_SPLIT_1KB is 4, 2KB 5,
 * 4KB 6 (gfx_7_2_enum.h). */
#define SI_TILE_SPLIT_MASK          0x00003800ul
#define SI_TILE_SPLIT_SHIFT         11u
#define SI_TILE_SPLIT_1KB           4ul
/* GRBM_GFX_INDEX: SE_BROADCAST (31), INSTANCE_BROADCAST (30),
 * SH_BROADCAST (29), SE_INDEX 23:16, SH_INDEX 15:8. */
#define SI_GRBM_INSTANCE_BROADCAST  0x40000000ul
#define SI_GRBM_SH_BROADCAST        0x20000000ul
#define SI_GRBM_SE_SHIFT            16u
#define SI_GRBM_SH_SHIFT            8u
/* The fused or disabled RBs, CUs and TCCs: each register's field 31:16
 * (BACKEND_DISABLE 23:16, INACTIVE_CUS 31:16, TCC_DISABLE 31:16). */
#define SI_DISABLE_SHIFT            16u
#define SI_RB_DISABLE_FIELD         0x000000fful
#define SI_CU_DISABLE_FIELD         0x0000fffful
#define SI_TCC_DISABLE_FIELD        0x0000fffful
/* PA_SC_RASTER_CONFIG's maps: RB_MAP_PKR0 1:0, RB_MAP_PKR1 3:2, PKR_MAP
 * 9:8, SE_MAP 25:24; RASTER_CONFIG_*_MAP_0 is 0 and _MAP_3 is 3. */
#define SI_RASTER_RB_MAP_PKR0_SHIFT 0u
#define SI_RASTER_RB_MAP_PKR1_SHIFT 2u
#define SI_RASTER_PKR_MAP_SHIFT     8u
#define SI_RASTER_SE_MAP_SHIFT      24u
#define SI_RASTER_MAP_FIELD         3ul
#define SI_RASTER_MAP_3             3ul
/* num_rb_pipes is at most 16, and write_harvested_raster_configs keeps
 * four engines' masks. */
#define SI_RB_PIPES_MAX             16ul
#define SI_HARVEST_SE_MASKS         4u
/* setup_spi's walk over SPI_STATIC_THREAD_MGMT_3: sixteen steps. */
#define SI_SPI_STEPS                16u
/* TCP_ADDR_CONFIG NUM_TCC_BANKS 3:0; a TCC index is a nibble of the
 * steer pair, eight to a register. */
#define SI_TCC_BANKS_MASK           0x0000000ful
#define SI_TCC_NIBBLES_PER_REG      8ul
/* CP_QUEUE_THRESHOLDS: ROQ_IB1_START 16h (7:0), ROQ_IB2_START 2Bh (15:8). */
#define SI_QUEUE_THRESHOLDS         (0x16ul | (0x2bul << 8))
/* CP_MEQ_THRESHOLDS: MEQ1_START 30h (7:0), MEQ2_START 60h (15:8). */
#define SI_MEQ_THRESHOLDS           (0x30ul | (0x60ul << 8))
/* SPI_CONFIG_CNTL_1: VTX_DONE_DELAY 4 at 3:0. */
#define SI_SPI_CONFIG_CNTL_1_VALUE  4ul
/* PA_SC_FIFO_SIZE: frontend 20h (5:0), the chip's backend (14:6), HiZ tile
 * 30h (20:15), early-Z tile 130h (31:23). Every SI chip's frontend, HiZ
 * and early-Z are these. */
#define SI_PA_SC_FIFO_SIZE_COMMON   ((0x20ul << 0) | (0x30ul << 15) | \
                                     (0x130ul << 23))
#define SI_PA_SC_FIFO_BACKEND_SHIFT 6u
/* PA_SC_FORCE_EOV_MAX_CNTS: CLK_CNT 4095 (15:0), REZ_CNT 255 (31:16). */
#define SI_FORCE_EOV_MAX            (4095ul | (255ul << 16))
/* VGT_CACHE_INVALIDATION: VC_AND_TC (2) at 1:0, ES_AND_GS_AUTO (3) at
 * 7:6. */
#define SI_VGT_CACHE_INVAL_VALUE    (2ul | (3ul << 6))
#define SI_VGT_GS_VERTEX_REUSE_VAL  16ul
/* PA_CL_ENHANCE: CLIP_VTX_REORDER_ENA (bit 0), NUM_CLIP_SEQ 3 (2:1). */
#define SI_PA_CL_ENHANCE_VALUE      (1ul | (3ul << 1))

/* The PCI ids each chip is claimed for (oland_hw16.c's rows). */
#define SI_DEVICE_OLAND_6610        0x6610ul
#define SI_DEVICE_OLAND_6611        0x6611ul
#define SI_DEVICE_VERDE_W4100       0x682cul
#define SI_DEVICE_PITCAIRN_W7000    0x6808ul
#define SI_DEVICE_TAHITI_W9000      0x6780ul

/* One chip: gfx_v6_0_constants_init's case for it, gfx_v6_0_raster_config's
 * value, and its golden lists and tile table. */
struct si_chip {
    v9x_u32 shader_engines;     /* max_shader_engines */
    v9x_u32 sh_per_se;          /* max_sh_per_se */
    v9x_u32 cu_per_sh;          /* max_cu_per_sh */
    v9x_u32 backends_per_se;    /* max_backends_per_se */
    v9x_u32 tccs;               /* max_texture_channel_caches */
    v9x_u32 fifo_backend;       /* sc_prim_fifo_size_backend */
    v9x_u32 gb_addr_golden;
    v9x_u32 raster_config;
    const struct v9x_vi_reg_seq *golden;
    v9x_u32 golden_count;
    const struct v9x_vi_reg_seq *golden_rlc;
    v9x_u32 golden_rlc_count;
    const struct v9x_vi_reg_seq *mgcg;
    v9x_u32 mgcg_count;
    const v9x_u32 *tile_modes;
    const v9x_u32 *row_split_modes;
};

/* Indexed by V9X_SI_CHIP_*. Raster configurations: TAHITI and PITCAIRN
 * RB_XSEL2 2 (5:4), RB_XSEL 1 (6), PKR_MAP 2 (9:8), PKR_YSEL 1 (13:12),
 * SE_MAP 2 (25:24), SE_XSEL 2 (27:26), SE_YSEL 2 (29:28); VERDE RB_XSEL
 * 1, PKR_MAP 2, PKR_YSEL 1; OLAND RB_YSEL (7) 1. */
static const struct si_chip si_chips[V9X_SI_CHIP_COUNT] = {
    {
        1ul, 1ul, 6ul, 2ul, 4ul, 0x40ul,
        SI_GB_ADDR_VERDE_GOLDEN, 0x00000080ul,
        v9x_si_oland_golden, V9X_SI_OLAND_GOLDEN_COUNT,
        v9x_si_oland_golden_rlc, V9X_SI_OLAND_GOLDEN_RLC_COUNT,
        v9x_si_oland_mgcg_cgcg_init, V9X_SI_OLAND_MGCG_CGCG_COUNT,
        v9x_si_oland_tile_modes, &v9x_si_oland_row_split_modes
    },
    {
        1ul, 2ul, 5ul, 4ul, 4ul, 0x40ul,
        SI_GB_ADDR_VERDE_GOLDEN, 0x00001240ul,
        v9x_si_verde_golden, V9X_SI_VERDE_GOLDEN_COUNT,
        v9x_si_verde_golden_rlc, V9X_SI_VERDE_GOLDEN_RLC_COUNT,
        v9x_si_verde_mgcg_cgcg_init, V9X_SI_VERDE_MGCG_CGCG_COUNT,
        v9x_si_verde_tile_modes, &v9x_si_verde_row_split_modes
    },
    {
        2ul, 2ul, 5ul, 4ul, 8ul, 0x100ul,
        SI_GB_ADDR_TAHITI_GOLDEN, 0x2a001260ul,
        v9x_si_pitcairn_golden, V9X_SI_PITCAIRN_GOLDEN_COUNT,
        v9x_si_pitcairn_golden_rlc, V9X_SI_PITCAIRN_GOLDEN_RLC_COUNT,
        v9x_si_pitcairn_mgcg_cgcg_init, V9X_SI_PITCAIRN_MGCG_CGCG_COUNT,
        v9x_si_tahiti_tile_modes, &v9x_si_tahiti_row_split_modes
    },
    {
        2ul, 2ul, 8ul, 4ul, 12ul, 0x100ul,
        SI_GB_ADDR_TAHITI_GOLDEN, 0x2a001260ul,
        v9x_si_tahiti_golden, V9X_SI_TAHITI_GOLDEN_COUNT,
        v9x_si_tahiti_golden_rlc, V9X_SI_TAHITI_GOLDEN_RLC_COUNT,
        v9x_si_tahiti_mgcg_cgcg_init, V9X_SI_TAHITI_MGCG_CGCG_COUNT,
        v9x_si_tahiti_tile_modes, &v9x_si_tahiti_row_split_modes
    }
};

struct si_ops_out {
    struct v9x_vi_reg_seq *ops;
    v9x_u32 count;
    v9x_u32 capacity;
};

static void si_op(struct si_ops_out *out, v9x_u32 offset, v9x_u32 clear,
                  v9x_u32 set)
{
    if (out->count < out->capacity) {
        out->ops[out->count].offset = offset;
        out->ops[out->count].clear = clear;
        out->ops[out->count].set = set;
    }
    ++out->count;
}

static void si_sequence(struct si_ops_out *out,
                        const struct v9x_vi_reg_seq *table, v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        si_op(out, table[index].offset, table[index].clear,
              table[index].set);
    }
}

/* amdgpu_gfx_create_bitmask. */
static v9x_u32 si_bitmask(v9x_u32 bits)
{
    if (bits >= 32ul) {
        return 0xfffffffful;
    }
    return (1ul << bits) - 1ul;
}

static v9x_u32 si_bit_count(v9x_u32 value)
{
    v9x_u32 count = 0ul;

    while (value != 0ul) {
        value &= value - 1ul;
        ++count;
    }
    return count;
}

v9x_u32 v9x_si_chip_for_device(v9x_u32 pci_device)
{
    if (pci_device == SI_DEVICE_OLAND_6610 ||
        pci_device == SI_DEVICE_OLAND_6611) {
        return V9X_SI_CHIP_OLAND;
    }
    if (pci_device == SI_DEVICE_VERDE_W4100) {
        return V9X_SI_CHIP_VERDE;
    }
    if (pci_device == SI_DEVICE_PITCAIRN_W7000) {
        return V9X_SI_CHIP_PITCAIRN;
    }
    if (pci_device == SI_DEVICE_TAHITI_W9000) {
        return V9X_SI_CHIP_TAHITI;
    }
    return V9X_SI_CHIP_NONE;
}

v9x_status v9x_si_chip_shape(v9x_u32 chip, v9x_u32 *shader_engines,
                             v9x_u32 *sh_per_se)
{
    if (chip >= V9X_SI_CHIP_COUNT || shader_engines == 0 || sh_per_se == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *shader_engines = si_chips[chip].shader_engines;
    *sh_per_se = si_chips[chip].sh_per_se;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_si_grbm_select(v9x_u32 se, v9x_u32 sh)
{
    return SI_GRBM_INSTANCE_BROADCAST | (se << SI_GRBM_SE_SHIFT) |
           (sh << SI_GRBM_SH_SHIFT);
}

/* The row in KiB from MC_ARB_RAMCFG, as constants_init computes
 * mem_row_size_in_kb: 1, 2 or 4. */
static v9x_u32 si_row_kib(v9x_u32 mc_arb_ramcfg)
{
    v9x_u32 columns = (mc_arb_ramcfg & SI_RAMCFG_NOOFCOLS_MASK) >>
                      SI_RAMCFG_NOOFCOLS_SHIFT;
    v9x_u32 kib = (4ul << (8u + columns)) / 1024ul;

    return kib > SI_ROW_KIB_MAX ? SI_ROW_KIB_MAX : kib;
}

/* setup_rb's harvested case, gfx_v6_0_write_harvested_raster_configs: each
 * engine selected in turn and given the map with its missing RBs, packers
 * or the other engine mapped away. rb_mask is the active RBs, num_rb
 * num_rb_pipes, as Linux passes them; se_mask[1] onwards shift the masked
 * se_mask[0], as Linux's do. */
static void si_harvested_raster(struct si_ops_out *out,
                                const struct si_chip *chip,
                                v9x_u32 raster, v9x_u32 rb_mask,
                                v9x_u32 num_rb)
{
    v9x_u32 sh_per_se = chip->sh_per_se > 1ul ? chip->sh_per_se : 1ul;
    v9x_u32 num_se = chip->shader_engines > 1ul ? chip->shader_engines : 1ul;
    v9x_u32 rb_per_pkr = num_rb / num_se / sh_per_se;
    v9x_u32 rb_per_se = num_rb / num_se;
    v9x_u32 se_mask[SI_HARVEST_SE_MASKS];
    v9x_u32 se;
    v9x_u32 index;

    if (rb_per_pkr > 2ul) {
        rb_per_pkr = 2ul;
    }
    se_mask[0] = si_bitmask(rb_per_se) & rb_mask;
    for (index = 1u; index < SI_HARVEST_SE_MASKS; ++index) {
        se_mask[index] = (se_mask[index - 1u] << rb_per_se) & rb_mask;
    }

    for (se = 0ul; se < num_se; ++se) {
        v9x_u32 value = raster;
        v9x_u32 pkr0_mask = si_bitmask(rb_per_pkr) << (se * rb_per_se);
        v9x_u32 pkr1_mask = pkr0_mask << rb_per_pkr;
        v9x_u32 pair = (se / 2ul) * 2ul;
        v9x_u32 rb0_mask;
        v9x_u32 rb1_mask;

        if (num_se > 1ul &&
            (se_mask[pair] == 0ul || se_mask[pair + 1ul] == 0ul)) {
            value &= ~(SI_RASTER_MAP_FIELD << SI_RASTER_SE_MAP_SHIFT);
            if (se_mask[pair] == 0ul) {
                value |= SI_RASTER_MAP_3 << SI_RASTER_SE_MAP_SHIFT;
            }
        }

        pkr0_mask &= rb_mask;
        pkr1_mask &= rb_mask;
        if (rb_per_se > 2ul && (pkr0_mask == 0ul || pkr1_mask == 0ul)) {
            value &= ~(SI_RASTER_MAP_FIELD << SI_RASTER_PKR_MAP_SHIFT);
            if (pkr0_mask == 0ul) {
                value |= SI_RASTER_MAP_3 << SI_RASTER_PKR_MAP_SHIFT;
            }
        }

        if (rb_per_se >= 2ul) {
            rb0_mask = (1ul << (se * rb_per_se)) & rb_mask;
            rb1_mask = (1ul << (se * rb_per_se + 1ul)) & rb_mask;
            if (rb0_mask == 0ul || rb1_mask == 0ul) {
                value &= ~(SI_RASTER_MAP_FIELD << SI_RASTER_RB_MAP_PKR0_SHIFT);
                if (rb0_mask == 0ul) {
                    value |= SI_RASTER_MAP_3 << SI_RASTER_RB_MAP_PKR0_SHIFT;
                }
            }
            if (rb_per_se > 2ul) {
                rb0_mask = (1ul << (se * rb_per_se + rb_per_pkr)) & rb_mask;
                rb1_mask = (1ul << (se * rb_per_se + rb_per_pkr + 1ul)) &
                           rb_mask;
                if (rb0_mask == 0ul || rb1_mask == 0ul) {
                    value &=
                        ~(SI_RASTER_MAP_FIELD << SI_RASTER_RB_MAP_PKR1_SHIFT);
                    if (rb0_mask == 0ul) {
                        value |=
                            SI_RASTER_MAP_3 << SI_RASTER_RB_MAP_PKR1_SHIFT;
                    }
                }
            }
        }

        /* select_se_sh(se, all arrays, all instances). */
        si_op(out, V9X_SI_GRBM_GFX_INDEX, SI_WRITE,
              SI_GRBM_INSTANCE_BROADCAST | SI_GRBM_SH_BROADCAST |
                  (se << SI_GRBM_SE_SHIFT));
        si_op(out, SI_PA_SC_RASTER_CONFIG, SI_WRITE, value);
    }
    si_op(out, V9X_SI_GRBM_GFX_INDEX, SI_WRITE, V9X_SI_GRBM_BROADCAST);
}

/* setup_rb: the active RBs, two bits an array (max_backends_per_se /
 * max_sh_per_se on every SI chip), in engine-then-array order; one
 * broadcast write of the chip's raster configuration if they are all
 * there or none reads as there, else the harvested writes. */
static void si_setup_rb(struct si_ops_out *out, const struct si_chip *chip,
                        const struct v9x_si_gpu_config *config)
{
    v9x_u32 width = chip->backends_per_se / chip->sh_per_se;
    v9x_u32 active = 0ul;
    v9x_u32 rb_pipes;
    v9x_u32 disabled;
    v9x_u32 se;
    v9x_u32 sh;

    for (se = 0ul; se < chip->shader_engines; ++se) {
        for (sh = 0ul; sh < chip->sh_per_se; ++sh) {
            disabled = (config->rb_disable[se][sh] >> SI_DISABLE_SHIFT) &
                       SI_RB_DISABLE_FIELD;
            active |= (~disabled & si_bitmask(width)) <<
                      ((se * chip->sh_per_se + sh) * width);
        }
    }
    rb_pipes = chip->backends_per_se * chip->shader_engines;
    if (rb_pipes > SI_RB_PIPES_MAX) {
        rb_pipes = SI_RB_PIPES_MAX;
    }

    si_op(out, V9X_SI_GRBM_GFX_INDEX, SI_WRITE, V9X_SI_GRBM_BROADCAST);
    if (active == 0ul || si_bit_count(active) >= rb_pipes) {
        si_op(out, SI_PA_SC_RASTER_CONFIG, SI_WRITE, chip->raster_config);
        return;
    }
    si_harvested_raster(out, chip, chip->raster_config, active, rb_pipes);
}

/* The value the golden list leaves in a register it writes whole, or 0
 * with *found clear. */
static v9x_u32 si_golden_value(const struct si_chip *chip, v9x_u32 offset,
                               int *found)
{
    v9x_u32 index;
    v9x_u32 value = 0ul;

    *found = 0;
    for (index = 0ul; index < chip->golden_count; ++index) {
        if (chip->golden[index].offset == offset &&
            chip->golden[index].clear == SI_WRITE) {
            value = chip->golden[index].set;
            *found = 1;
        }
    }
    return value;
}

/* setup_tcc: nothing unless a TCC is off. Then the TCP-to-TCC steering the
 * golden list wrote, with the disabled TCCs' nibbles taken out in order,
 * and as many banks as TCCs remain. Linux reads the steering back; the
 * golden list writes both registers whole on every SI chip, so their
 * values are known here. Returns 0 if they are not in the list. */
static int si_setup_tcc(struct si_ops_out *out, const struct si_chip *chip,
                        const struct v9x_si_gpu_config *config)
{
    v9x_u32 disabled = si_bitmask(chip->tccs) &
                       ((config->tcc_disable >> SI_DISABLE_SHIFT) &
                        SI_TCC_DISABLE_FIELD);
    v9x_u32 steer[2];
    v9x_u32 patched[2];
    v9x_u32 max_active;
    v9x_u32 active = 0ul;
    v9x_u32 index;
    v9x_u32 tcc;
    int found_lo;
    int found_hi;

    if (disabled == 0ul) {
        return 1;
    }
    steer[0] = si_golden_value(chip, SI_TCP_CHAN_STEER_LO, &found_lo);
    steer[1] = si_golden_value(chip, SI_TCP_CHAN_STEER_HI, &found_hi);
    if (!found_lo || !found_hi) {
        return 0;
    }
    max_active = chip->tccs - si_bit_count(disabled);
    patched[0] = 0ul;
    patched[1] = 0ul;
    for (index = 0ul; index < chip->tccs; ++index) {
        tcc = (steer[index / SI_TCC_NIBBLES_PER_REG] >>
               ((index % SI_TCC_NIBBLES_PER_REG) * 4ul)) & 0xful;
        if (((1ul << tcc) & disabled) == 0ul) {
            patched[active / SI_TCC_NIBBLES_PER_REG] |=
                tcc << ((active % SI_TCC_NIBBLES_PER_REG) * 4ul);
            ++active;
        }
        if (active == max_active) {
            break;
        }
    }
    si_op(out, SI_TCP_ADDR_CONFIG, SI_TCC_BANKS_MASK,
          (active - 1ul) & SI_TCC_BANKS_MASK);
    si_op(out, SI_TCP_CHAN_STEER_HI, SI_WRITE, patched[1]);
    si_op(out, SI_TCP_CHAN_STEER_LO, SI_WRITE, patched[0]);
    return 1;
}

/* setup_spi: in each engine and array, SPI_STATIC_THREAD_MGMT_3's bit for
 * the first active CU Linux's walk finds, cleared. The walk shifts its
 * mask by the step number each step (1, 2, 8, 64, ...), so after CU 1 it
 * tests CU 3, not CU 2; it is Linux's, kept. An array where it finds none
 * is selected and left alone. */
static void si_setup_spi(struct si_ops_out *out, const struct si_chip *chip,
                         const struct v9x_si_gpu_config *config)
{
    v9x_u32 active;
    v9x_u32 mask;
    v9x_u32 step;
    v9x_u32 se;
    v9x_u32 sh;

    for (se = 0ul; se < chip->shader_engines; ++se) {
        for (sh = 0ul; sh < chip->sh_per_se; ++sh) {
            si_op(out, V9X_SI_GRBM_GFX_INDEX, SI_WRITE,
                  v9x_si_grbm_select(se, sh));
            active = ~((config->cu_disable[se][sh] >> SI_DISABLE_SHIFT) &
                       SI_CU_DISABLE_FIELD) &
                     si_bitmask(chip->cu_per_sh);
            mask = 1ul;
            for (step = 0u; step < SI_SPI_STEPS; ++step) {
                mask <<= step;
                if ((active & mask) != 0ul) {
                    si_op(out, SI_SPI_STATIC_THREAD_MGMT_3, mask, 0ul);
                    break;
                }
            }
        }
    }
    si_op(out, V9X_SI_GRBM_GFX_INDEX, SI_WRITE, V9X_SI_GRBM_BROADCAST);
}

v9x_u32 v9x_si_gpu_init_ops_for(const struct v9x_si_gpu_config *config,
                                struct v9x_vi_reg_seq *ops,
                                v9x_u32 capacity)
{
    const struct si_chip *chip;
    struct si_ops_out out;
    v9x_u32 gb_addr_config;
    v9x_u32 row_kib;
    v9x_u32 row_field;
    v9x_u32 split;
    v9x_u32 mode;
    v9x_u32 index;

    if (ops == 0 || config == 0 || config->chip >= V9X_SI_CHIP_COUNT) {
        return 0ul;
    }
    chip = &si_chips[config->chip];
    out.ops = ops;
    out.count = 0ul;
    out.capacity = capacity;

    /* si_init_golden_registers. Tahiti's fourth list and Cape Verde's
     * have no graphics entry (gen-oland-gpu-init.ps1 asserts it). */
    si_sequence(&out, chip->golden, chip->golden_count);
    si_sequence(&out, chip->golden_rlc, chip->golden_rlc_count);
    si_sequence(&out, chip->mgcg, chip->mgcg_count);

    /* gfx_v6_0_constants_init: the row, then GB_ADDR_CONFIG with it and
     * the engine count. ROW_SIZE is 0, 1, 2 for 1, 2, 4 KiB, and the
     * tile split 1KB, 2KB, 4KB. */
    row_kib = si_row_kib(config->mc_arb_ramcfg);
    row_field = row_kib == 4ul ? 2ul : (row_kib == 2ul ? 1ul : 0ul);
    split = SI_TILE_SPLIT_1KB + row_field;
    gb_addr_config = (chip->gb_addr_golden & ~SI_GB_ADDR_ROW_SIZE_MASK) |
                     (row_field << SI_GB_ADDR_ROW_SIZE_SHIFT);
    gb_addr_config &= ~SI_GB_ADDR_NUM_SE_MASK;
    if (chip->shader_engines == 2ul) {
        gb_addr_config |= 1ul << SI_GB_ADDR_NUM_SE_SHIFT;
    }

    si_op(&out, SI_GRBM_CNTL, SI_WRITE, SI_GRBM_READ_TIMEOUT);
    si_op(&out, SI_GB_ADDR_CONFIG, SI_WRITE, gb_addr_config);
    si_op(&out, SI_HDP_ADDR_CONFIG, SI_WRITE, gb_addr_config);
    si_op(&out, SI_DMA0_TILING_CONFIG, SI_WRITE, gb_addr_config);
    si_op(&out, SI_DMA1_TILING_CONFIG, SI_WRITE, gb_addr_config);
    for (index = 0ul; index < V9X_SI_TILE_MODES; ++index) {
        mode = chip->tile_modes[index];
        if ((*chip->row_split_modes & (1ul << index)) != 0ul) {
            mode = (mode & ~SI_TILE_SPLIT_MASK) |
                   (split << SI_TILE_SPLIT_SHIFT);
        }
        si_op(&out, SI_GB_TILE_MODE0 + index * 4ul, SI_WRITE, mode);
    }

    si_setup_rb(&out, chip, config);
    if (!si_setup_tcc(&out, chip, config)) {
        return 0ul;
    }
    si_setup_spi(&out, chip, config);

    si_op(&out, SI_CP_QUEUE_THRESHOLDS, SI_WRITE, SI_QUEUE_THRESHOLDS);
    si_op(&out, SI_CP_MEQ_THRESHOLDS, SI_WRITE, SI_MEQ_THRESHOLDS);
    si_op(&out, SI_SPI_CONFIG_CNTL_1, SI_WRITE, SI_SPI_CONFIG_CNTL_1_VALUE);
    si_op(&out, SI_PA_SC_FIFO_SIZE, SI_WRITE,
          SI_PA_SC_FIFO_SIZE_COMMON |
              (chip->fifo_backend << SI_PA_SC_FIFO_BACKEND_SHIFT));
    si_op(&out, SI_VGT_NUM_INSTANCES, SI_WRITE, 1ul);
    si_op(&out, SI_CP_PERFMON_CNTL, SI_WRITE, 0ul);
    si_op(&out, SI_SQ_CONFIG, SI_WRITE, 0ul);
    si_op(&out, SI_PA_SC_FORCE_EOV_MAX, SI_WRITE, SI_FORCE_EOV_MAX);
    si_op(&out, SI_VGT_CACHE_INVALIDATION, SI_WRITE,
          SI_VGT_CACHE_INVAL_VALUE);
    si_op(&out, SI_VGT_GS_VERTEX_REUSE, SI_WRITE, SI_VGT_GS_VERTEX_REUSE_VAL);
    si_op(&out, SI_PA_SC_LINE_STIPPLE_STATE, SI_WRITE, 0ul);
    for (index = 0ul; index < SI_CB_PERFCOUNTER_SELECTS; ++index) {
        si_op(&out, SI_CB_PERFCOUNTER0_SELECT0 + index * 4ul, SI_WRITE, 0ul);
    }
    si_op(&out, SI_PA_CL_ENHANCE, SI_WRITE, SI_PA_CL_ENHANCE_VALUE);

    return out.count <= capacity ? out.count : 0ul;
}

/* What V9XSIMM read on W9QI0DAD's and M-BXMFA8's Olands: MC_ARB_RAMCFG
 * 000012A1h (NOOFCOLS 2, a 4 KiB row), CC_GC_SHADER_ARRAY_CONFIG
 * FFC00000h (CUs 0-5 active), CC_RB_BACKEND_DISABLE 0; and V9XSICP, which
 * ran this list, CGTS_TCC_DISABLE and CGTS_USER_TCC_DISABLE 0
 * (docs\decisions\2026-11-16-oland-gpu-init.md). */
#define SI_OLAND_MC_ARB_RAMCFG      0x000012a1ul
#define SI_OLAND_CU_DISABLE         0xffc00000ul

v9x_u32 v9x_si_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity)
{
    struct v9x_si_gpu_config config;
    v9x_u32 se;
    v9x_u32 sh;

    config.chip = V9X_SI_CHIP_OLAND;
    config.mc_arb_ramcfg = SI_OLAND_MC_ARB_RAMCFG;
    config.tcc_disable = 0ul;
    for (se = 0ul; se < V9X_SI_MAX_SE; ++se) {
        for (sh = 0ul; sh < V9X_SI_MAX_SH; ++sh) {
            config.rb_disable[se][sh] = 0ul;
            config.cu_disable[se][sh] = SI_OLAND_CU_DISABLE;
        }
    }
    return v9x_si_gpu_init_ops_for(&config, ops, capacity);
}
