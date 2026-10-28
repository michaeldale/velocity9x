/*
 * V9XCZMM.EXE: AMD Carrizo (Radeon R7E, GCN 3) read-only register snapshot.
 *
 * Phase 2 of docs\plans\ati-carrizo-gfx8.md. Finds 1002:9874 by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), loads
 * CZMM.VXD, and asks it for register offsets through the 256 KiB register
 * BAR (BAR5), each read twice, and for the SMU firmware header's version,
 * microcode load status and signature. Writes C:\V9XDIAG\V9XCZMM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux amdgpu's headers;
 *   [Smu]     the firmware header fields and, bit by bit, which engines'
 *             microcode the SMU reports loaded;
 *   [Decode]  the carve-out's size and where the memory controller and the
 *             CPU see it, the GART, and the CP, RLC and SDMA engines' state;
 *   [D1]-[D3] each display controller's scanout.
 *
 * What the plan needs from it: whether any engine already has microcode or
 * is power-gated as the system BIOS left it, where video memory sits in the
 * GPU's address space and in system memory, and whether the VBE scanout
 * address is the FB start plus the LFB offset. The VxD writes one register,
 * the SMU's indirect index, and puts it back (carrizo_mmio_probe.asm).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_CZMM_PATH        V9X_DIAG_PATH("V9XCZMM.INI")
#define V9X_CZMM_VXD         "\\\\.\\CZMM.VXD"
#define V9X_CZMM_MAGIC       0x4d4d5a43ul
#define V9X_CZMM_CAPTURE     1u
#define V9X_CZMM_OFFSET_MAX  256u
#define V9X_CZMM_HEADER      16u
#define V9X_CZMM_MMIO_BYTES  0x00040000ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        16ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul
#define V9X_DEVICE_CARRIZO   0x9874ul

#define V9X_ST_ATI             0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_BAR_64          0x00000040ul
#define V9X_ST_NOT_CARRIZO     0x00000080ul
#define V9X_ST_ADDRESS_INVALID 0x00000100ul
#define V9X_ST_SMU_READ        0x00000200ul

/* Register offsets the decode reads back: Linux amdgpu's gca/gfx_8_0_d.h,
 * gmc/gmc_8_1_d.h, oss/oss_3_0_d.h, bif/bif_5_0_d.h and dce/dce_11_0_d.h
 * in build\external\carrizo\linux\, dword indices times four. */
#define V9X_CZ_SRBM_SOFT_RESET     0x0e60ul
#define V9X_CZ_VM_L2_CNTL          0x1400ul
#define V9X_CZ_VM_CONTEXT0_CNTL    0x1410ul
#define V9X_CZ_MC_VM_FB_LOCATION   0x2024ul
#define V9X_CZ_MC_VM_L1_TLB_CNTL   0x2064ul
#define V9X_CZ_MC_VM_FB_OFFSET     0x2068ul
#define V9X_CZ_CONFIG_MEMSIZE      0x5428ul
#define V9X_CZ_GRBM_STATUS         0x8010ul
#define V9X_CZ_GRBM_SOFT_RESET     0x8020ul
#define V9X_CZ_CP_MEC_CNTL         0x8234ul
#define V9X_CZ_CP_ME_CNTL          0x86d8ul
#define V9X_CZ_SDMA0_STATUS_REG    0xd034ul
#define V9X_CZ_SDMA0_F32_CNTL      0xd048ul
#define V9X_CZ_SDMA0_GFX_RB_CNTL   0xd200ul
#define V9X_CZ_SDMA1_OFFSET        0x0800ul
#define V9X_CZ_RLC_CNTL            0x3b000ul
#define V9X_CZ_RLC_GPM_STAT        0x3b100ul
#define V9X_CZ_RLC_PG_CNTL         0x3b10cul
#define V9X_CZ_GRPH_ENABLE         0x6800ul
#define V9X_CZ_GRPH_CONTROL        0x6804ul
#define V9X_CZ_GRPH_PRIMARY        0x6810ul
#define V9X_CZ_GRPH_PITCH          0x6818ul
#define V9X_CZ_GRPH_PRIMARY_HIGH   0x681cul
#define V9X_CZ_GRPH_UPDATE         0x6844ul
#define V9X_CZ_VIEWPORT_SIZE       0x6d74ul
#define V9X_CZ_CRTC_CONTROL        0x6e70ul
#define V9X_CZ_CRTC_STATUS         0x6e8cul

/* Field layouts, the *_sh_mask.h beside the same headers.
 * SRBM_SOFT_RESET bit 20 SDMA0, bit 6 SDMA1. GRBM_SOFT_RESET bit 0 CP,
 * bit 2 RLC, bit 16 GFX. VM_L2_CNTL, VM_CONTEXT0_CNTL and
 * MC_VM_MX_L1_TLB_CNTL bit 0 enable. MC_VM_FB_LOCATION 15:0 start and
 * 31:16 end, 16 MiB units of the 40-bit GPU address (gmc_v8_0.c
 * vram_gtt_location). MC_VM_FB_OFFSET in 4 MiB units of the system
 * physical address (gmc_v8_0.c mc_init, APUs). GRBM_STATUS bit 31
 * GUI_ACTIVE. CP_ME_CNTL bit 24 CE, 26 PFP, 28 ME halted. CP_MEC_CNTL
 * bit 30 ME1, 28 ME2 halted. RLC_CNTL bit 0 the RLC running. RLC_GPM_STAT
 * bit 0 busy, bit 1 GFX powered, bit 2 GFX clocked. RLC_PG_CNTL bit 0 GFX
 * power gating enabled. SDMA0_F32_CNTL bit 0 halted. SDMA0_STATUS_REG
 * bit 0 idle. SDMA0_GFX_RB_CNTL bit 0 enable, 5:1 log2 of the ring dwords.
 * GRPH_CONTROL 1:0 depth, 10:8 format, 23:20 array (tiling) mode.
 * GRPH_UPDATE bit 2 surface update pending. CRTC_CONTROL bit 0 master
 * enable. CRTC_STATUS bit 0 in vertical blank. */
#define V9X_CZ_SOFT_RESET_SDMA0    0x00100000ul
#define V9X_CZ_SOFT_RESET_SDMA1    0x00000040ul
#define V9X_CZ_SOFT_RESET_CP       0x00000001ul
#define V9X_CZ_SOFT_RESET_RLC      0x00000004ul
#define V9X_CZ_SOFT_RESET_GFX      0x00010000ul
#define V9X_CZ_ENABLE_BIT          0x00000001ul
#define V9X_CZ_FB_FIELD_MASK       0xfffful
#define V9X_CZ_FB_UNIT_SHIFT       24u
#define V9X_CZ_FB_OFFSET_SHIFT     22u
#define V9X_CZ_GUI_ACTIVE          0x80000000ul
#define V9X_CZ_CP_CE_HALT          0x01000000ul
#define V9X_CZ_CP_PFP_HALT         0x04000000ul
#define V9X_CZ_CP_ME_HALT          0x10000000ul
#define V9X_CZ_MEC_ME1_HALT        0x40000000ul
#define V9X_CZ_MEC_ME2_HALT        0x10000000ul
#define V9X_CZ_RLC_BUSY            0x00000001ul
#define V9X_CZ_GFX_POWERED         0x00000002ul
#define V9X_CZ_GFX_CLOCKED         0x00000004ul
#define V9X_CZ_SDMA_HALT           0x00000001ul
#define V9X_CZ_SDMA_IDLE           0x00000001ul
#define V9X_CZ_RB_SIZE_SHIFT       1u
#define V9X_CZ_RB_SIZE_MASK        0x1ful
#define V9X_CZ_GRPH_DEPTH_MASK     0x3ul
#define V9X_CZ_GRPH_FORMAT_SHIFT   8u
#define V9X_CZ_GRPH_FORMAT_MASK    0x7ul
#define V9X_CZ_GRPH_ARRAY_SHIFT    20u
#define V9X_CZ_GRPH_ARRAY_MASK     0xful
#define V9X_CZ_UPDATE_PENDING      0x00000004ul
#define V9X_CZ_IN_VBLANK           0x00000001ul
#define V9X_CZ_VIEWPORT_MASK       0x3ffful

/* Carrizo's three display controllers (dce_v11_0.c: num_crtc = 3), at
 * these offsets from the first (dce_11_0_d.h). */
#define V9X_CZ_CRTC_COUNT 3u
static const DWORD v9x_crtc_offsets[V9X_CZ_CRTC_COUNT] = {
    0x0000ul, 0x0800ul, 0x1000ul
};

/* UcodeLoadStatus, one bit per microcode image (smu_ucode_xfer_cz.h). */
struct v9x_czmm_ucode {
    DWORD mask;
    const char *name;
};

static const struct v9x_czmm_ucode v9x_ucodes[] = {
    { 0x00000001ul, "Sdma0" },
    { 0x00000002ul, "Sdma1" },
    { 0x00000004ul, "CpCe" },
    { 0x00000008ul, "CpPfp" },
    { 0x00000010ul, "CpMe" },
    { 0x00000020ul, "CpMecJt1" },
    { 0x00000040ul, "CpMecJt2" },
    { 0x00000080ul, "GmconReng" },
    { 0x00000100ul, "RlcG" },
    { 0x00000200ul, "RlcScratch" },
    { 0x00000400ul, "RlcSrmAram" },
    { 0x00000800ul, "RlcSrmDram" },
    { 0x00001000ul, "DmcuEram" },
    { 0x00002000ul, "DmcuIram" }
};

#define V9X_UCODE_COUNT (sizeof(v9x_ucodes) / sizeof(v9x_ucodes[0]))

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_czmm_register {
    DWORD offset;
    const char *name;
};

/* Engine, memory controller, bus and SMU mailbox state. */
static const struct v9x_czmm_register v9x_global[] = {
    { 0x00300ul, "VGA_RENDER_CONTROL" },
    { 0x00310ul, "VGA_MEMORY_BASE_ADDRESS" },
    { 0x00324ul, "VGA_MEMORY_BASE_ADDRESS_HIGH" },
    { 0x00328ul, "VGA_HDP_CONTROL" },
    { 0x00330ul, "D1VGA_CONTROL" },
    { 0x00338ul, "D2VGA_CONTROL" },
    { 0x00700ul, "SMU_MP1_SRBM2P_MSG_0" },
    { 0x00740ul, "SMU_MP1_SRBM2P_RESP_0" },
    { 0x00780ul, "SMU_MP1_SRBM2P_ARG_0" },
    { 0x00e4cul, "SRBM_STATUS2" },
    { 0x00e50ul, "SRBM_STATUS" },
    { 0x00e60ul, "SRBM_SOFT_RESET" },
    { 0x01400ul, "VM_L2_CNTL" },
    { 0x01410ul, "VM_CONTEXT0_CNTL" },
    { 0x0153cul, "VM_CONTEXT0_PAGE_TABLE_BASE_ADDR" },
    { 0x0155cul, "VM_CONTEXT0_PAGE_TABLE_START_ADDR" },
    { 0x0157cul, "VM_CONTEXT0_PAGE_TABLE_END_ADDR" },
    { 0x02004ul, "MC_SHARED_CHMAP" },
    { 0x02024ul, "MC_VM_FB_LOCATION" },
    { 0x02028ul, "MC_VM_AGP_TOP" },
    { 0x0202cul, "MC_VM_AGP_BOT" },
    { 0x02030ul, "MC_VM_AGP_BASE" },
    { 0x02034ul, "MC_VM_SYSTEM_APERTURE_LOW_ADDR" },
    { 0x02038ul, "MC_VM_SYSTEM_APERTURE_HIGH_ADDR" },
    { 0x0203cul, "MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR" },
    { 0x02064ul, "MC_VM_MX_L1_TLB_CNTL" },
    { 0x02068ul, "MC_VM_FB_OFFSET" },
    { 0x02760ul, "MC_ARB_RAMCFG" },
    { 0x02c04ul, "HDP_NONSURFACE_BASE" },
    { 0x02c08ul, "HDP_NONSURFACE_INFO" },
    { 0x02c0cul, "HDP_NONSURFACE_SIZE" },
    { 0x05428ul, "CONFIG_MEMSIZE" },
    { 0x05490ul, "BIF_FB_EN" },
    { 0x08000ul, "GRBM_CNTL" },
    { 0x08008ul, "GRBM_STATUS2" },
    { 0x0800cul, "GRBM_PWR_CNTL" },
    { 0x08010ul, "GRBM_STATUS" },
    { 0x08014ul, "GRBM_STATUS_SE0" },
    { 0x08018ul, "GRBM_STATUS_SE1" },
    { 0x08020ul, "GRBM_SOFT_RESET" },
    { 0x08210ul, "CP_CPC_STATUS" },
    { 0x0821cul, "CP_CPF_STATUS" },
    { 0x08234ul, "CP_MEC_CNTL" },
    { 0x08674ul, "CP_STALLED_STAT1" },
    { 0x0867cul, "CP_BUSY_STAT" },
    { 0x08680ul, "CP_STAT" },
    { 0x086d8ul, "CP_ME_CNTL" },
    { 0x08700ul, "CP_RB0_RPTR" },
    { 0x08708ul, "CP_RB_WPTR_POLL_CNTL" },
    { 0x089bcul, "CC_GC_SHADER_ARRAY_CONFIG" },
    { 0x089c0ul, "GC_USER_SHADER_ARRAY_CONFIG" },
    { 0x098f4ul, "CC_RB_BACKEND_DISABLE" },
    { 0x098f8ul, "GB_ADDR_CONFIG" },
    { 0x09910ul, "GB_TILE_MODE0" },
    { 0x09914ul, "GB_TILE_MODE1" },
    { 0x09918ul, "GB_TILE_MODE2" },
    { 0x0991cul, "GB_TILE_MODE3" },
    { 0x09920ul, "GB_TILE_MODE4" },
    { 0x09924ul, "GB_TILE_MODE5" },
    { 0x09928ul, "GB_TILE_MODE6" },
    { 0x0992cul, "GB_TILE_MODE7" },
    { 0x09990ul, "GB_MACROTILE_MODE0" },
    { 0x0c100ul, "CP_RB0_BASE" },
    { 0x0c104ul, "CP_RB0_CNTL" },
    { 0x0c10cul, "CP_RB0_RPTR_ADDR" },
    { 0x0c110ul, "CP_RB0_RPTR_ADDR_HI" },
    { 0x0c114ul, "CP_RB0_WPTR" },
    { 0x0c144ul, "CP_RB_VMID" },
    { 0x0c1a8ul, "CP_INT_CNTL_RING0" },
    { 0x0c2c4ul, "CP_RB0_BASE_HI" },
    { 0x0d010ul, "SDMA0_CNTL" },
    { 0x0d018ul, "SDMA0_TILING_CONFIG" },
    { 0x0d034ul, "SDMA0_STATUS_REG" },
    { 0x0d038ul, "SDMA0_STATUS1_REG" },
    { 0x0d048ul, "SDMA0_F32_CNTL" },
    { 0x0d058ul, "SDMA_POWER_GATING" },
    { 0x0d070ul, "SDMA0_ID" },
    { 0x0d074ul, "SDMA0_VERSION" },
    { 0x0d200ul, "SDMA0_GFX_RB_CNTL" },
    { 0x0d204ul, "SDMA0_GFX_RB_BASE" },
    { 0x0d208ul, "SDMA0_GFX_RB_BASE_HI" },
    { 0x0d20cul, "SDMA0_GFX_RB_RPTR" },
    { 0x0d210ul, "SDMA0_GFX_RB_WPTR" },
    { 0x0d228ul, "SDMA0_GFX_IB_CNTL" },
    { 0x0d248ul, "SDMA0_GFX_DOORBELL" },
    { 0x0d810ul, "SDMA1_CNTL" },
    { 0x0d834ul, "SDMA1_STATUS_REG" },
    { 0x0d848ul, "SDMA1_F32_CNTL" },
    { 0x0d874ul, "SDMA1_VERSION" },
    { 0x0da00ul, "SDMA1_GFX_RB_CNTL" },
    { 0x0da0cul, "SDMA1_GFX_RB_RPTR" },
    { 0x0da10ul, "SDMA1_GFX_RB_WPTR" },
    { 0x30100ul, "SCRATCH_REG0" },
    { 0x30800ul, "GRBM_GFX_INDEX" },
    { 0x3b000ul, "RLC_CNTL" },
    { 0x3b010ul, "RLC_STAT" },
    { 0x3b014ul, "RLC_SAFE_MODE" },
    { 0x3b100ul, "RLC_GPM_STAT" },
    { 0x3b10cul, "RLC_PG_CNTL" },
    { 0x3b114ul, "RLC_GPM_THREAD_ENABLE" },
    { 0x3b124ul, "RLC_CGCG_CGLS_CTRL" },
    { 0x3b14cul, "RLC_PG_ALWAYS_ON_CU_MASK" },
    { 0x3b184ul, "RLC_SERDES_CU_MASTER_BUSY" },
    { 0x3b18cul, "RLC_GPM_GENERAL_0" }
};

/* One display controller's block, at the first controller's offsets. */
static const struct v9x_czmm_register v9x_crtc[] = {
    { 0x6800ul, "GRPH_ENABLE" },
    { 0x6804ul, "GRPH_CONTROL" },
    { 0x680cul, "GRPH_SWAP_CNTL" },
    { 0x6810ul, "GRPH_PRIMARY_SURFACE_ADDRESS" },
    { 0x6814ul, "GRPH_SECONDARY_SURFACE_ADDRESS" },
    { 0x6818ul, "GRPH_PITCH" },
    { 0x681cul, "GRPH_PRIMARY_SURFACE_ADDRESS_HIGH" },
    { 0x6824ul, "GRPH_SURFACE_OFFSET_X" },
    { 0x6828ul, "GRPH_SURFACE_OFFSET_Y" },
    { 0x6834ul, "GRPH_X_END" },
    { 0x6838ul, "GRPH_Y_END" },
    { 0x6844ul, "GRPH_UPDATE" },
    { 0x6848ul, "GRPH_FLIP_CONTROL" },
    { 0x684cul, "GRPH_SURFACE_ADDRESS_INUSE" },
    { 0x6998ul, "CUR_CONTROL" },
    { 0x699cul, "CUR_SURFACE_ADDRESS" },
    { 0x69a4ul, "CUR_SURFACE_ADDRESS_HIGH" },
    { 0x6d70ul, "VIEWPORT_START" },
    { 0x6d74ul, "VIEWPORT_SIZE" },
    { 0x6e00ul, "CRTC_H_TOTAL" },
    { 0x6e1cul, "CRTC_V_TOTAL" },
    { 0x6e34ul, "CRTC_V_BLANK_START_END" },
    { 0x6e70ul, "CRTC_CONTROL" },
    { 0x6e74ul, "CRTC_BLANK_CONTROL" },
    { 0x6e8cul, "CRTC_STATUS" },
    { 0x6e90ul, "CRTC_STATUS_POSITION" },
    { 0x6e98ul, "CRTC_STATUS_FRAME_COUNT" },
    { 0x6ea0ul, "CRTC_STATUS_HV_COUNT" },
    { 0x6ed4ul, "CRTC_UPDATE_LOCK" },
    { 0x6ef4ul, "CRTC_MASTER_UPDATE_LOCK" },
    { 0x6ef8ul, "CRTC_MASTER_UPDATE_MODE" }
};

#define V9X_GLOBAL_COUNT (sizeof(v9x_global) / sizeof(v9x_global[0]))
#define V9X_CRTC_REG_COUNT (sizeof(v9x_crtc) / sizeof(v9x_crtc[0]))

/* Every offset fits one request: the VxD takes 256. */
typedef char v9x_czmm_list_fits[
    V9X_GLOBAL_COUNT + V9X_CRTC_REG_COUNT * V9X_CZ_CRTC_COUNT <=
    V9X_CZMM_OFFSET_MAX ? 1 : -1];

static const char * const v9x_grph_depths[4] = { "8", "16", "32", "64" };

struct v9x_czmm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_CZMM_OFFSET_MAX];
};

struct v9x_czmm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar5;
    DWORD accepted;
    DWORD memsize;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD smu_index;
    DWORD smu_version;
    DWORD smu_load_status;
    DWORD smu_signature;
    DWORD reserved[3];
    DWORD offsets[V9X_CZMM_OFFSET_MAX];
    DWORD first[V9X_CZMM_OFFSET_MAX];
    DWORD second[V9X_CZMM_OFFSET_MAX];
};

/* The header must be the VxD's CZ_HEADER_DWORDS long. */
typedef char v9x_czmm_header_matches[
    offsetof(struct v9x_czmm_result, offsets) ==
    sizeof(DWORD) * V9X_CZMM_HEADER ? 1 : -1];

static struct v9x_czmm_request v9x_request;
static struct v9x_czmm_result v9x_result;
static DWORD v9x_values[V9X_CZMM_MMIO_BYTES / 4u];

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(V9X_PCI_ADDRESS);
    v9x_outpd(V9X_PCI_ADDRESS, device | (offset & 0xfcul));
    value = v9x_inpd(V9X_PCI_DATA);
    v9x_outpd(V9X_PCI_ADDRESS, saved);
    return value;
}

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_decimal(char *text, DWORD value)
{
    char digits[11];
    int count;
    int at;

    count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    at = 0;
    while (count != 0) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, V9X_CZMM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_CZMM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_CZMM_PATH);
}

static void v9x_yes_no(const char *section, const char *key, int value)
{
    v9x_text(section, key, value ? "yes" : "no");
}

/* Offsets reach 3B18Ch, so five hex digits. */
static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 5);
}

/* Appends one offset and its name to the request; the name is written
 * with a display controller's prefix where it has one. */
static void v9x_request_add(DWORD offset, const char *prefix,
                            const char *name)
{
    char key[24];
    char label[64];

    v9x_request.offsets[v9x_request.count++] = offset;
    v9x_offset_key(key, "Name.", offset);
    lstrcpyA(label, prefix);
    lstrcatA(label, name);
    v9x_text("Mmio", key, label);
}

static void v9x_request_build(void)
{
    static const char * const prefixes[V9X_CZ_CRTC_COUNT] = {
        "D1.", "D2.", "D3."
    };
    DWORD index;
    DWORD crtc;

    v9x_request.count = 0ul;
    for (index = 0ul; index < V9X_GLOBAL_COUNT; ++index) {
        v9x_request_add(v9x_global[index].offset, "", v9x_global[index].name);
    }
    for (crtc = 0ul; crtc < V9X_CZ_CRTC_COUNT; ++crtc) {
        for (index = 0ul; index < V9X_CRTC_REG_COUNT; ++index) {
            v9x_request_add(v9x_crtc[index].offset + v9x_crtc_offsets[crtc],
                            prefixes[crtc], v9x_crtc[index].name);
        }
    }
}

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_CZMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_CZMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_CZMM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_SNAPSHOT_TAKEN) != 0ul;
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;
    DWORD offset;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        offset = v9x_result.offsets[index];
        v9x_offset_key(key, "R", offset);
        v9x_hex(text, v9x_result.first[index], 8);
        if (v9x_result.second[index] != v9x_result.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_result.second[index], 8);
        }
        v9x_text("Mmio", key, text);
        v9x_values[offset >> 2] = v9x_result.first[index];
    }
}

static DWORD v9x_value(DWORD offset)
{
    return v9x_values[offset >> 2];
}

/* The SMU firmware header, and the load status bit by bit. */
static void v9x_publish_smu(void)
{
    DWORD index;

    if ((v9x_result.status & V9X_ST_SMU_READ) == 0ul) {
        v9x_text("Smu", "Read", "no");
        return;
    }
    v9x_text("Smu", "Read", "yes");
    v9x_key("Smu", "IndexBefore", v9x_result.smu_index);
    v9x_key("Smu", "Version", v9x_result.smu_version);
    v9x_number("Smu", "VersionMajor", (v9x_result.smu_version >> 16) & 0xfful);
    v9x_number("Smu", "VersionMinor", (v9x_result.smu_version >> 8) & 0xfful);
    v9x_number("Smu", "VersionPatch", v9x_result.smu_version & 0xfful);
    v9x_key("Smu", "UcodeLoadStatus", v9x_result.smu_load_status);
    v9x_key("Smu", "Signature", v9x_result.smu_signature);
    for (index = 0ul; index < V9X_UCODE_COUNT; ++index) {
        v9x_yes_no("Smu", v9x_ucodes[index].name,
                   (v9x_result.smu_load_status & v9x_ucodes[index].mask) != 0ul);
    }
}

/* The first 1002:9874 on buses 0-15; 0 if none. An APU's GPU sits on bus
 * 0 beside the host bridge (M-XP7053's Enum key: BUS_00&DEV_01&FUNC_00),
 * but the scan does not assume it. */
static DWORD v9x_find_carrizo(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;
    DWORD id;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            id = v9x_config_read(config, 0ul);
            if ((id & 0xfffful) == V9X_VENDOR_ATI &&
                (id >> 16) == V9X_DEVICE_CARRIZO) {
                return config;
            }
        }
    }
    return 0ul;
}

static void v9x_decode_engine(const char *prefix, DWORD offset)
{
    char key[32];
    DWORD value;

    lstrcpyA(key, prefix);
    lstrcatA(key, "Halted");
    v9x_yes_no("Decode", key,
               (v9x_value(V9X_CZ_SDMA0_F32_CNTL + offset) &
                V9X_CZ_SDMA_HALT) != 0ul);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Idle");
    v9x_yes_no("Decode", key,
               (v9x_value(V9X_CZ_SDMA0_STATUS_REG + offset) &
                V9X_CZ_SDMA_IDLE) != 0ul);
    value = v9x_value(V9X_CZ_SDMA0_GFX_RB_CNTL + offset);
    lstrcpyA(key, prefix);
    lstrcatA(key, "RingEnabled");
    v9x_yes_no("Decode", key, (value & V9X_CZ_ENABLE_BIT) != 0ul);
    lstrcpyA(key, prefix);
    lstrcatA(key, "RingLog2Dwords");
    v9x_number("Decode", key,
               (value >> V9X_CZ_RB_SIZE_SHIFT) & V9X_CZ_RB_SIZE_MASK);
}

/* The memory controller, the GART and the engines. FB_LOCATION's fields
 * are 16 MiB units of a 40-bit address, so the low 32 bits of the start
 * are the field's low byte shifted up and bits 39:32 its high byte.
 * FB_OFFSET is in 4 MiB units, so bits 9:0 give the low 32 bits of the
 * carve-out's system address and the rest bits 63:32. */
static void v9x_decode_global(void)
{
    DWORD value;

    v9x_number("Decode", "MemoryMiB", v9x_value(V9X_CZ_CONFIG_MEMSIZE));

    value = v9x_value(V9X_CZ_MC_VM_FB_LOCATION);
    v9x_key("Decode", "FbStartHigh",
            (value & V9X_CZ_FB_FIELD_MASK) >> 8);
    v9x_key("Decode", "FbStartLow",
            (value & 0xfful) << V9X_CZ_FB_UNIT_SHIFT);
    v9x_key("Decode", "FbEndHigh",
            ((value >> 16) & V9X_CZ_FB_FIELD_MASK) >> 8);
    v9x_key("Decode", "FbEndLow",
            (((value >> 16) & 0xfful) << V9X_CZ_FB_UNIT_SHIFT) | 0x00fffffful);

    value = v9x_value(V9X_CZ_MC_VM_FB_OFFSET);
    v9x_key("Decode", "CarveoutSystemHigh", value >> (32u - V9X_CZ_FB_OFFSET_SHIFT));
    v9x_key("Decode", "CarveoutSystemLow", value << V9X_CZ_FB_OFFSET_SHIFT);

    v9x_yes_no("Decode", "GartL2Enabled",
               (v9x_value(V9X_CZ_VM_L2_CNTL) & V9X_CZ_ENABLE_BIT) != 0ul);
    v9x_yes_no("Decode", "GartContext0Enabled",
               (v9x_value(V9X_CZ_VM_CONTEXT0_CNTL) & V9X_CZ_ENABLE_BIT) != 0ul);
    v9x_yes_no("Decode", "L1TlbEnabled",
               (v9x_value(V9X_CZ_MC_VM_L1_TLB_CNTL) & V9X_CZ_ENABLE_BIT) != 0ul);
    v9x_yes_no("Decode", "GuiActive",
               (v9x_value(V9X_CZ_GRBM_STATUS) & V9X_CZ_GUI_ACTIVE) != 0ul);

    value = v9x_value(V9X_CZ_GRBM_SOFT_RESET);
    v9x_yes_no("Decode", "CpHeldInReset", (value & V9X_CZ_SOFT_RESET_CP) != 0ul);
    v9x_yes_no("Decode", "RlcHeldInReset", (value & V9X_CZ_SOFT_RESET_RLC) != 0ul);
    v9x_yes_no("Decode", "GfxHeldInReset", (value & V9X_CZ_SOFT_RESET_GFX) != 0ul);

    value = v9x_value(V9X_CZ_CP_ME_CNTL);
    v9x_yes_no("Decode", "CpCeHalted", (value & V9X_CZ_CP_CE_HALT) != 0ul);
    v9x_yes_no("Decode", "CpPfpHalted", (value & V9X_CZ_CP_PFP_HALT) != 0ul);
    v9x_yes_no("Decode", "CpMeHalted", (value & V9X_CZ_CP_ME_HALT) != 0ul);
    value = v9x_value(V9X_CZ_CP_MEC_CNTL);
    v9x_yes_no("Decode", "MecMe1Halted", (value & V9X_CZ_MEC_ME1_HALT) != 0ul);
    v9x_yes_no("Decode", "MecMe2Halted", (value & V9X_CZ_MEC_ME2_HALT) != 0ul);

    v9x_yes_no("Decode", "RlcRunning",
               (v9x_value(V9X_CZ_RLC_CNTL) & V9X_CZ_ENABLE_BIT) != 0ul);
    value = v9x_value(V9X_CZ_RLC_GPM_STAT);
    v9x_yes_no("Decode", "RlcBusy", (value & V9X_CZ_RLC_BUSY) != 0ul);
    v9x_yes_no("Decode", "GfxPowered", (value & V9X_CZ_GFX_POWERED) != 0ul);
    v9x_yes_no("Decode", "GfxClocked", (value & V9X_CZ_GFX_CLOCKED) != 0ul);
    v9x_yes_no("Decode", "GfxPowerGatingEnabled",
               (v9x_value(V9X_CZ_RLC_PG_CNTL) & V9X_CZ_ENABLE_BIT) != 0ul);

    value = v9x_value(V9X_CZ_SRBM_SOFT_RESET);
    v9x_yes_no("Decode", "Sdma0HeldInReset",
               (value & V9X_CZ_SOFT_RESET_SDMA0) != 0ul);
    v9x_yes_no("Decode", "Sdma1HeldInReset",
               (value & V9X_CZ_SOFT_RESET_SDMA1) != 0ul);
    v9x_decode_engine("Sdma0", 0ul);
    v9x_decode_engine("Sdma1", V9X_CZ_SDMA1_OFFSET);
}

/* Each display controller's scanout. LfbOffset is the primary surface
 * address less the FB start, which the plan expects to equal the VBE
 * framebuffer's offset into BAR0 for the controller VBE drives. */
static void v9x_decode_crtc(DWORD crtc)
{
    static const char * const sections[V9X_CZ_CRTC_COUNT] = {
        "D1", "D2", "D3"
    };
    const char *section;
    DWORD base;
    DWORD value;
    DWORD fb_low;

    section = sections[crtc];
    base = v9x_crtc_offsets[crtc];

    v9x_yes_no(section, "CrtcEnabled",
               (v9x_value(V9X_CZ_CRTC_CONTROL + base) &
                V9X_CZ_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "GrphEnabled",
               (v9x_value(V9X_CZ_GRPH_ENABLE + base) &
                V9X_CZ_ENABLE_BIT) != 0ul);

    value = v9x_value(V9X_CZ_GRPH_CONTROL + base);
    v9x_text(section, "BitsPerPixel",
             v9x_grph_depths[value & V9X_CZ_GRPH_DEPTH_MASK]);
    v9x_number(section, "Format",
               (value >> V9X_CZ_GRPH_FORMAT_SHIFT) & V9X_CZ_GRPH_FORMAT_MASK);
    v9x_number(section, "ArrayMode",
               (value >> V9X_CZ_GRPH_ARRAY_SHIFT) & V9X_CZ_GRPH_ARRAY_MASK);
    v9x_number(section, "PitchPixels", v9x_value(V9X_CZ_GRPH_PITCH + base));

    value = v9x_value(V9X_CZ_VIEWPORT_SIZE + base);
    v9x_number(section, "ViewportWidth",
               (value >> 16) & V9X_CZ_VIEWPORT_MASK);
    v9x_number(section, "ViewportHeight", value & V9X_CZ_VIEWPORT_MASK);

    v9x_key(section, "SurfaceHigh",
            v9x_value(V9X_CZ_GRPH_PRIMARY_HIGH + base));
    v9x_key(section, "SurfaceLow", v9x_value(V9X_CZ_GRPH_PRIMARY + base));
    fb_low = (v9x_value(V9X_CZ_MC_VM_FB_LOCATION) & 0xfful) <<
             V9X_CZ_FB_UNIT_SHIFT;
    v9x_key(section, "LfbOffset",
            v9x_value(V9X_CZ_GRPH_PRIMARY + base) - fb_low);

    v9x_yes_no(section, "UpdatePending",
               (v9x_value(V9X_CZ_GRPH_UPDATE + base) &
                V9X_CZ_UPDATE_PENDING) != 0ul);
    v9x_yes_no(section, "InVblank",
               (v9x_value(V9X_CZ_CRTC_STATUS + base) &
                V9X_CZ_IN_VBLANK) != 0ul);
}

void __stdcall V9xCarrizoMmioEntry(void)
{
    DWORD config;
    DWORD crtc;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_CZMM_PATH);
    v9x_text("Report", "Tool", "V9XCZMM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only, smu-index-restored");

    config = v9x_find_carrizo();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-CARRIZO");
        WritePrivateProfileStringA(0, 0, 0, V9X_CZMM_PATH);
        ExitProcess(1u);
    }
    v9x_key("Report", "Config", config);
    v9x_key("Report", "Id", v9x_config_read(config, 0x00ul));
    v9x_key("Report", "CommandStatus", v9x_config_read(config, 0x04ul));
    v9x_key("Report", "ClassRevision", v9x_config_read(config, 0x08ul));
    v9x_key("Report", "Bar0", v9x_config_read(config, 0x10ul));
    v9x_key("Report", "Bar0High", v9x_config_read(config, 0x14ul));
    v9x_key("Report", "Bar2", v9x_config_read(config, 0x18ul));
    v9x_key("Report", "Bar2High", v9x_config_read(config, 0x1cul));
    v9x_key("Report", "Bar4", v9x_config_read(config, 0x20ul));
    v9x_key("Report", "Bar5", v9x_config_read(config, 0x24ul));
    v9x_key("Report", "Subsystem", v9x_config_read(config, 0x2cul));
    v9x_key("Report", "RomBase", v9x_config_read(config, 0x30ul));

    v9x_request.config = config;
    v9x_request_build();
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "MemSize", v9x_result.memsize);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_CZMM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_publish_smu();
    v9x_decode_global();
    for (crtc = 0ul; crtc < V9X_CZ_CRTC_COUNT; ++crtc) {
        v9x_decode_crtc(crtc);
    }
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_CZMM_PATH);
    ExitProcess(0u);
}
