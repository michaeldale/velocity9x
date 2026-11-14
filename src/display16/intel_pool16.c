/*
 * The Gen3 texture pool at Enable: decide whether this boot gets one, have
 * the mini-VDD allocate it, judge every page it hands back, and have it
 * mapped. docs\plans\intel-gen3-system-memory-textures.md, Phase 3 design;
 * gate docs\decisions\2026-11-12-intel-gen3-texture-pool-decisions.md.
 *
 * The split is mtrr.c's: the mini-VDD allocates and writes PTEs and
 * interprets nothing (V9XMINI_FN_I9XX_POOL_* in V9XMAPI.INC); the decision
 * is src\common\i9xx_pool.c, host-tested; this applies it. The outcome is
 * one line of [IntelPool] keys in INTELGTT.TXT, so a boot without a pool
 * says which rule declined it.
 *
 * LEAFCODE: called far from intel_hw16.c's diagnostics hook, after the GTT
 * inventory. It calls i9xx_pool.c near (the same segment) and otherwise
 * only far entries: runtime.asm and KERNEL.
 */
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/i9xx_pool.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/leaf16.h"

/* V9XMAPI.INC's numbers and acceptance word. */
#define V9X_POOL_FN_INFO    0x002Cu
#define V9X_POOL_FN_ALLOC   0x002Du
#define V9X_POOL_FN_CHUNK   0x002Eu
#define V9X_POOL_FN_MAP     0x002Fu
#define V9X_POOL_ACCEPT     0x4C4F4F50ul
#define V9X_POOL_STATE_MAPPED 2ul

/* The gate: MICHAEL-NETBOOK's 945GSE, and every GM965, measured on TOMMOHP
 * (docs\decisions\2026-11-14-intel-gm965-texture-pool-decisions.md). The
 * mini-VDD checks the device again. */
#define V9X_POOL_DEVICE       0x27AEu
#define V9X_POOL_DEVICE_GM965 0x2A02u

/* The 945's GMADR is 256 MiB (MSAC; 2026-09-12-intel-phase2-gtt-
 * inventory.md, 65,536 entries). */
#define V9X_POOL_APERTURE   0x10000000ul

/* The VMM page-table entry's present bit (VMM.INC P_PRES). */
#define V9X_POOL_PTE_PRESENT 0x00000001ul

#define V9X_POOL_SECTION    "IntelPool"

/* EBX, ECX, EDX, ESI after the last pool call (runtime.asm). Not static:
 * the thunk stores into it by name. */
DWORD v9x_i9xx_pool_regs[4];

/* The pool this boot has, for gma950_hw16.c's engine_pool hook. */
DWORD v9x_intel_pool_offset;
DWORD v9x_intel_pool_bytes;

extern WORD FAR PASCAL V9xMiniI9xxPoolCall(WORD function, DWORD ebx_in,
                                           DWORD ecx_in, DWORD edx_in);
extern void FAR PASCAL V9xEnsureDiagDir(void);
extern WORD v9x_intel_runtime3d_allowed;
extern DWORD v9x_i9xx_bsm;
extern WORD v9x_i9xx_ggc;
extern WORD v9x_map_pages_hi;
extern WORD v9x_map_pages_lo;

static void v9x_pool_hex(const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[9];
    short shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 0x0ful];
    }
    text[8] = '\0';
    WritePrivateProfileString(V9X_POOL_SECTION, key, text,
                              V9X_DIAG_INTELGTT_TXT);
}

static void v9x_pool_result(const char *result, DWORD reason)
{
    WritePrivateProfileString(V9X_POOL_SECTION, "Result", result,
                              V9X_DIAG_INTELGTT_TXT);
    v9x_pool_hex("Reason", reason);
}

/* GGC's GMS, bits 7:4, in bytes: intel_gtt16.c's table, which is static
 * there and in a segment this one cannot call near. */
static DWORD v9x_pool_stolen_bytes(WORD ggc)
{
    static const WORD sizes[16] = {
        0u, 1u, 4u, 8u, 16u, 32u, 48u, 64u,
        128u, 256u, 96u, 160u, 224u, 352u, 0u, 0u
    };
    return (DWORD)sizes[(ggc >> 4) & 15u] * 1024ul * 1024ul;
}

/* Hand the pages back unmapped after the driver refused them. */
static void v9x_pool_release(void)
{
    (void)V9xMiniI9xxPoolCall(V9X_POOL_FN_MAP, 0ul, 0ul, 0ul);
}

/*
 * Every page the mini-VDD allocated, four at a time, through the per-page
 * check. Returns V9X_I9XX_POOL_OK, or the first refusal with its index.
 */
static v9x_u16 v9x_pool_check_pages(const struct v9x_i9xx_pool_layout *layout,
                                    DWORD count, DWORD *refused_at)
{
    DWORD chunk;
    WORD slot;
    v9x_u16 reason;

    for (chunk = 0ul; chunk < count / 4ul; ++chunk) {
        if (V9xMiniI9xxPoolCall(V9X_POOL_FN_CHUNK, 0ul, chunk, 0ul) == 0u) {
            *refused_at = chunk * 4ul;
            return V9X_I9XX_POOL_COUNT;
        }
        for (slot = 0u; slot < 4u; ++slot) {
            DWORD entry = v9x_i9xx_pool_regs[slot];

            *refused_at = chunk * 4ul + slot;
            if ((entry & V9X_POOL_PTE_PRESENT) == 0ul) {
                return V9X_I9XX_POOL_ZERO_PAGE;
            }
            reason = v9x_i9xx_pool_check_page(layout, entry & 0xfffff000ul);
            if (reason != V9X_I9XX_POOL_OK) {
                return reason;
            }
        }
    }
    return V9X_I9XX_POOL_OK;
}

void V9X_LEAF_FAR v9x_intel_pool_setup(unsigned short device_id)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    DWORD phys_pages;
    DWORD refused_at = 0ul;
    v9x_u16 reason;

    v9x_intel_pool_offset = 0ul;
    v9x_intel_pool_bytes = 0ul;
    V9xEnsureDiagDir();
    WritePrivateProfileString(V9X_POOL_SECTION, 0, 0, V9X_DIAG_INTELGTT_TXT);

    if (device_id != V9X_POOL_DEVICE && device_id != V9X_POOL_DEVICE_GM965) {
        v9x_pool_result("not-gated-device", 0ul);
        return;
    }
    /* V9X3D OFF from DOS turns the pool off with the engine (decision 3). */
    if (v9x_intel_runtime3d_allowed == 0u) {
        v9x_pool_result("3d-off", 0ul);
        return;
    }
    if (V9xMiniI9xxPoolCall(V9X_POOL_FN_INFO, 0ul, 0ul, 0ul) == 0u) {
        v9x_pool_result("no-minivdd", 0ul);
        return;
    }
    phys_pages = v9x_i9xx_pool_regs[0];
    v9x_pool_hex("PhysPages", phys_pages);
    v9x_pool_hex("StateBefore", v9x_i9xx_pool_regs[1]);

    layout.bsm = v9x_i9xx_bsm;
    layout.stolen_bytes = v9x_pool_stolen_bytes(v9x_i9xx_ggc);
    layout.aperture_bytes = V9X_POOL_APERTURE;
    layout.gtt_entries = V9X_I9XX_GTT_ENTRY_COUNT;
    layout.mapped_bytes = (((DWORD)v9x_map_pages_hi << 16) |
                           (DWORD)v9x_map_pages_lo) + 1ul;
    layout.ram_bytes = phys_pages >= 0x00100000ul ? 0xfffff000ul
                                                  : phys_pages * 4096ul;
    layout.excluded_count = 0u;
    reason = v9x_i9xx_pool_size(&layout, &plan);
    v9x_pool_hex("MappedBytes", layout.mapped_bytes);
    v9x_pool_hex("PlanOffset", plan.aperture_offset);
    v9x_pool_hex("PlanBytes", plan.bytes);
    if (reason != V9X_I9XX_POOL_OK) {
        v9x_pool_result("refused-plan", reason);
        return;
    }

    /* A ReEnable finds the pool this boot already mapped: keep it. The
     * mode set does not touch its PTEs (Phase 2 record). */
    if (v9x_i9xx_pool_regs[1] == V9X_POOL_STATE_MAPPED) {
        v9x_intel_pool_offset = plan.aperture_offset;
        v9x_intel_pool_bytes = plan.bytes;
        v9x_pool_result("kept", 0ul);
        return;
    }
    if (v9x_i9xx_pool_regs[1] != 0ul) {
        v9x_pool_release();
    }

    if (V9xMiniI9xxPoolCall(V9X_POOL_FN_ALLOC, plan.first_entry,
                            plan.entry_count, 0ul) == 0u) {
        v9x_pool_result("refused-alloc", v9x_i9xx_pool_regs[2]);
        return;
    }
    reason = v9x_pool_check_pages(&layout, plan.entry_count, &refused_at);
    if (reason != V9X_I9XX_POOL_OK) {
        v9x_pool_hex("RefusedPage", refused_at);
        v9x_pool_release();
        v9x_pool_result("refused-page", reason);
        return;
    }
    if (V9xMiniI9xxPoolCall(V9X_POOL_FN_MAP, V9X_POOL_ACCEPT, layout.bsm,
                            layout.stolen_bytes) == 0u) {
        v9x_pool_hex("Detail", v9x_i9xx_pool_regs[3]);
        v9x_pool_result("refused-map", v9x_i9xx_pool_regs[2]);
        return;
    }
    v9x_intel_pool_offset = plan.aperture_offset;
    v9x_intel_pool_bytes = plan.bytes;
    v9x_pool_result("mapped", 0ul);
}
