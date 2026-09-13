#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/build.h"
#include "velocity9x/intel_gma.h"

#define V9X_I9XX_SCRATCH_TARGET_DELTA 0x100ul
#define V9X_I9XX_SCRATCH_WIDTH        8u
#define V9X_I9XX_SCRATCH_HEIGHT       8u
#define V9X_I9XX_SCRATCH_PITCH        32u
#define V9X_I9XX_SCRATCH_COLOR        0x55aa33ccul

extern unsigned long v9x_vbe_vram_reported;
extern DWORD v9x_i9xx_bsm;
extern WORD FAR PASCAL V9xPciReadIntelFlushPage(DWORD FAR *value);
extern void FAR PASCAL V9xEnsureDiagDir(void);
extern const char *v9x_intel_boot_token_mover_state(void);
extern void v9x_intel_phase4_maybe_run(
    const struct v9x_i9xx_sandbox_layout *layout,
    const DWORD *probe, const DWORD *blt, WORD flush_stable);
DWORD v9x_i9xx_ring_memory_value;
DWORD v9x_i9xx_ring_exec_head;
DWORD v9x_i9xx_ring_exec_tail;
DWORD v9x_i9xx_ring_exec_elapsed;
DWORD v9x_i9xx_ring_exec_polls;
DWORD v9x_i9xx_ring_exec_failure;
DWORD v9x_i9xx_ring_diag_value;

static void v9x_ring_hex32(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    short shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 0x0ful];
    }
    text[8] = '\0';
}

static void v9x_ring_write_hex(const char *key, DWORD value)
{
    char text[9];
    v9x_ring_hex32(text, value);
    WritePrivateProfileString("IntelRing", key, text,
                              V9X_DIAG_INTELRNG_TXT);
}

static void v9x_ring_dword_key(char *key, char prefix, WORD index)
{
    key[0] = prefix;
    key[1] = 'D';
    key[2] = (char)('0' + index);
    key[3] = '\0';
}

/*
 * Publish the complete proposed Phase 4 stream without touching ring memory
 * or MMIO. The guarded executor can run only after this first, no-write
 * capture has been collected on the exact binary and separately armed.
 */
void v9x_intel_publish_ring_plan(void)
{
    static WORD published_this_load;
    struct v9x_i9xx_sandbox_layout layout;
    DWORD probe[2];
    DWORD blt[8];
    DWORD combined[10];
    DWORD flush_page_0 = 0ul;
    DWORD flush_page_1 = 0ul;
    DWORD written;
    WORD index;
    WORD flush_page_read_0;
    WORD flush_page_read_1;
    WORD flush_page_stable;
    char key[4];

    /* Enable and mode restore both publish diagnostics. Preserve the first
     * armed step log (or partial failure) across every later call. */
    if (published_this_load != 0u) { return; }
    published_this_load = 1u;
    V9xEnsureDiagDir();
    WritePrivateProfileString("IntelRing", 0, 0, V9X_DIAG_INTELRNG_TXT);
    WritePrivateProfileString("IntelRing", "Access", "no-hardware-writes",
                              V9X_DIAG_INTELRNG_TXT);
    WritePrivateProfileString("IntelRing", "TokenMover",
                              v9x_intel_boot_token_mover_state(),
                              V9X_DIAG_INTELRNG_TXT);
    WritePrivateProfileString("IntelRing", "CaptureBuildId",
                              v9x_get_build_identity()->build_id,
                              V9X_DIAG_INTELRNG_TXT);
    WritePrivateProfileString("IntelRing", "ErrataGate", "0",
                              V9X_DIAG_INTELRNG_TXT);
    flush_page_read_0 = V9xPciReadIntelFlushPage(&flush_page_0);
    flush_page_read_1 = V9xPciReadIntelFlushPage(&flush_page_1);
    flush_page_stable = flush_page_read_0 != 0u &&
        flush_page_read_1 != 0u && flush_page_0 == flush_page_1 &&
        flush_page_0 != 0xfffffffful;
    v9x_ring_write_hex("FlushPageCfg0", flush_page_0);
    v9x_ring_write_hex("FlushPageCfg1", flush_page_1);
    WritePrivateProfileString("IntelRing", "FlushPageRead",
                              flush_page_stable != 0u ? "STABLE" : "REVIEW",
                              V9X_DIAG_INTELRNG_TXT);
    if (v9x_vbe_vram_reported == 0ul || v9x_i9xx_bsm == 0ul ||
        v9x_i9xx_sandbox_calculate(v9x_vbe_vram_reported, v9x_i9xx_bsm,
                                   &layout) != V9X_STATUS_OK) {
        WritePrivateProfileString("IntelRing", "Result", "LAYOUT-FAILED",
                                  V9X_DIAG_INTELRNG_TXT);
        return;
    }
    if (v9x_i9xx_build_mi_probe(probe, 2ul, &written) != V9X_STATUS_OK ||
        written != 2ul ||
        v9x_i9xx_build_color_blt(
            layout.scratch_offset + V9X_I9XX_SCRATCH_TARGET_DELTA,
            V9X_I9XX_SCRATCH_WIDTH, V9X_I9XX_SCRATCH_HEIGHT,
            V9X_I9XX_SCRATCH_PITCH, V9X_I9XX_SCRATCH_COLOR,
            layout.scratch_offset, layout.scratch_bytes,
            blt, 6ul, &written) != V9X_STATUS_OK || written != 6ul) {
        WritePrivateProfileString("IntelRing", "Result", "BUILD-FAILED",
                                  V9X_DIAG_INTELRNG_TXT);
        return;
    }
    blt[6] = V9X_I9XX_MI_FLUSH;
    blt[7] = V9X_I9XX_MI_NOOP;
    if (v9x_i9xx_decode_phase4_stream(
            probe, 2ul, layout.scratch_offset, layout.scratch_bytes) !=
            V9X_STATUS_OK ||
        v9x_i9xx_decode_phase4_stream(
            blt, 8ul, layout.scratch_offset, layout.scratch_bytes) !=
            V9X_STATUS_OK) {
        WritePrivateProfileString("IntelRing", "Result", "DECODE-FAILED",
                                  V9X_DIAG_INTELRNG_TXT);
        return;
    }

    v9x_ring_write_hex("HeapBytes", layout.heap_bytes);
    v9x_ring_write_hex("ReserveOffset", layout.reserve_offset);
    v9x_ring_write_hex("ReservePhysical", layout.reserve_physical);
    v9x_ring_write_hex("RingOffset", layout.ring_offset);
    v9x_ring_write_hex("RingPhysical", layout.ring_physical);
    v9x_ring_write_hex("RingBytes", layout.ring_bytes);
    v9x_ring_write_hex("RingCtl", (layout.ring_bytes - 4096ul) | 1ul);
    v9x_ring_write_hex("HwsOffset", layout.hws_offset);
    v9x_ring_write_hex("HwsPhysical", layout.hws_physical);
    v9x_ring_write_hex("ScratchOffset", layout.scratch_offset);
    v9x_ring_write_hex("ScratchPhysical", layout.scratch_physical);
    v9x_ring_write_hex("ScratchBytes", layout.scratch_bytes);
    for (index = 0u; index < 2u; ++index) {
        v9x_ring_dword_key(key, 'P', index);
        v9x_ring_write_hex(key, probe[index]);
        combined[index] = probe[index];
    }
    for (index = 0u; index < 8u; ++index) {
        v9x_ring_dword_key(key, 'B', index);
        v9x_ring_write_hex(key, blt[index]);
        combined[index + 2u] = blt[index];
    }
    v9x_ring_write_hex("ProbeCrc", v9x_i9xx_crc32_dwords(probe, 2ul));
    v9x_ring_write_hex("BltCrc", v9x_i9xx_crc32_dwords(blt, 8ul));
    v9x_ring_write_hex("ArmPacketCrc",
                       v9x_i9xx_crc32_dwords(combined, 10ul));
    v9x_ring_write_hex("WrapNoopDwords",
                       (layout.ring_bytes - V9X_I9XX_RING_GUARD_BYTES) / 4ul);
    v9x_ring_write_hex("ArmExecutionCrc",
                       v9x_i9xx_phase4_execution_crc(probe, blt));
    WritePrivateProfileString("IntelRing", "Result",
                              flush_page_stable != 0u ? "ERRATA-GATED" :
                                                       "FLUSH-PROBE-REVIEW",
                              V9X_DIAG_INTELRNG_TXT);
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_INTELRNG_TXT);
    v9x_intel_phase4_maybe_run(&layout, probe, blt, flush_page_stable);
}
