#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"

#define V9X_GTT_CHUNK_DWORDS 4u
#define V9X_GTT_BUFFER_DWORDS 256u
#define V9X_GTT_RUN_LOG_MAX 256ul

DWORD v9x_i9xx_gtt_chunk[V9X_GTT_CHUNK_DWORDS];
DWORD v9x_i9xx_gtt_buffer[V9X_GTT_BUFFER_DWORDS];
DWORD v9x_i9xx_gtt_bar3;
DWORD v9x_i9xx_gmadr_bar2;
DWORD v9x_i9xx_bsm;
WORD v9x_i9xx_ggc;
DWORD v9x_i9xx_gtt_hash_a;
DWORD v9x_i9xx_gtt_hash_b;

extern DWORD v9x_i9xx_first[V9X_I9XX_SNAPSHOT_DWORDS];
extern unsigned long v9x_vbe_vram_reported;
extern WORD FAR PASCAL V9xPciReadIntelGttConfig(void);
extern WORD FAR PASCAL V9xMiniI9xxGttCapture(DWORD bar3);
extern WORD FAR PASCAL V9xMiniI9xxGttChunk(WORD chunk);
extern DWORD FAR PASCAL V9xGmadrRead(DWORD offset);
extern void FAR PASCAL V9xEnsureDiagDir(void);

struct v9x_gtt_run {
    DWORD start;
    DWORD count;
    DWORD first_physical;
    DWORD previous_physical;
    DWORD previous_raw;
    DWORD stride;
    WORD present;
    WORD attributes;
};

static void v9x_hex32(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    short shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 0x0ful];
    }
    text[8] = '\0';
}

static void v9x_write_hex(const char *key, DWORD value)
{
    char text[9];
    v9x_hex32(text, value);
    WritePrivateProfileString("IntelGtt", key, text, V9X_DIAG_INTELGTT_TXT);
}

static void v9x_run_key(char *key, DWORD run, char field)
{
    static const char digits[] = "0123456789ABCDEF";
    key[0] = 'R'; key[1] = 'u'; key[2] = 'n';
    key[3] = digits[(run >> 12) & 0x0ful];
    key[4] = digits[(run >> 8) & 0x0ful];
    key[5] = digits[(run >> 4) & 0x0ful];
    key[6] = digits[run & 0x0ful];
    key[7] = field;
    key[8] = '\0';
}

static void v9x_write_run(const struct v9x_gtt_run *run, DWORD number)
{
    static const char fields[] = "SNPDA";
    DWORD values[5];
    WORD index;
    char key[9];
    if (number >= V9X_GTT_RUN_LOG_MAX) { return; }
    values[0] = run->start;
    values[1] = run->count;
    values[2] = run->first_physical;
    values[3] = run->stride;
    values[4] = run->attributes;
    for (index = 0u; index < 5u; ++index) {
        v9x_run_key(key, number, fields[index]);
        v9x_write_hex(key, values[index]);
    }
}

static void v9x_start_run(struct v9x_gtt_run *run, DWORD entry, DWORD raw)
{
    struct v9x_i9xx_pte pte;
    v9x_i9xx_decode_pte(raw, &pte);
    run->start = entry;
    run->count = 1ul;
    run->first_physical = pte.physical_page;
    run->previous_physical = pte.physical_page;
    run->previous_raw = raw;
    run->stride = 0xfffffffful;
    run->present = pte.present;
    run->attributes = (WORD)(raw & V9X_I9XX_PTE_ATTRIBUTE_MASK);
}

static WORD v9x_run_accepts(struct v9x_gtt_run *run, DWORD raw)
{
    struct v9x_i9xx_pte pte;
    DWORD stride;
    v9x_i9xx_decode_pte(raw, &pte);
    if (pte.present != run->present ||
        (WORD)(raw & V9X_I9XX_PTE_ATTRIBUTE_MASK) != run->attributes) {
        return 0u;
    }
    if (pte.present == 0u) { return raw == run->previous_raw; }
    stride = pte.physical_page - run->previous_physical;
    if (stride != 0ul && stride != V9X_I9XX_GTT_PAGE_BYTES) { return 0u; }
    if (run->count > 1ul && stride != run->stride) { return 0u; }
    if (run->count == 1ul) { run->stride = stride; }
    return 1u;
}

static DWORD v9x_stolen_bytes(WORD ggc)
{
    static const DWORD sizes[8] = {
        0ul, 1ul, 4ul, 8ul, 16ul, 32ul, 48ul, 64ul
    };
    return sizes[(ggc >> 4) & 7u] * 1024ul * 1024ul;
}

void v9x_intel_publish_gtt_inventory(void)
{
    struct v9x_i9xx_gtt_inventory inventory;
    struct v9x_gtt_run run;
    DWORD stolen;
    DWORD entry;
    DWORD run_number = 0ul;
    DWORD reserve_raw = 0ul;
    DWORD sample_zero = 0ul;
    DWORD sample_reserve = 0ul;
    WORD chunk;
    WORD slot;
    WORD buffered = 0u;
    HFILE binary = HFILE_ERROR;
    WORD failed = 0u;

    V9xEnsureDiagDir();
    WritePrivateProfileString("IntelGtt", 0, 0, V9X_DIAG_INTELGTT_TXT);
    WritePrivateProfileString("IntelGtt", "Access", "read-only",
                              V9X_DIAG_INTELGTT_TXT);
    WritePrivateProfileString("IntelGtt", "BarProvenance", "PCI-BAR3-runtime",
                              V9X_DIAG_INTELGTT_TXT);
    if (V9xPciReadIntelGttConfig() == 0u) {
        WritePrivateProfileString("IntelGtt", "Result", "CONFIG-FAILED",
                                  V9X_DIAG_INTELGTT_TXT);
        return;
    }
    stolen = v9x_stolen_bytes(v9x_i9xx_ggc);
    if (stolen == 0ul || v9x_vbe_vram_reported == 0ul ||
        V9xMiniI9xxGttCapture(v9x_i9xx_gtt_bar3) == 0u ||
        v9x_i9xx_gtt_inventory_begin(
            &inventory, V9X_I9XX_GTT_ENTRY_COUNT, v9x_i9xx_bsm, stolen,
            v9x_vbe_vram_reported, v9x_i9xx_first[0]) != V9X_STATUS_OK) {
        WritePrivateProfileString("IntelGtt", "Result", "CAPTURE-FAILED",
                                  V9X_DIAG_INTELGTT_TXT);
        return;
    }

    binary = _lcreat(V9X_DIAG_INTELGTT_BIN, 0);
    if (binary == HFILE_ERROR) {
        WritePrivateProfileString("IntelGtt", "Result", "FILE-FAILED",
                                  V9X_DIAG_INTELGTT_TXT);
        return;
    }
    run.count = 0ul;
    for (chunk = 0u; chunk <
         (WORD)(V9X_I9XX_GTT_ENTRY_COUNT / V9X_GTT_CHUNK_DWORDS); ++chunk) {
        if (V9xMiniI9xxGttChunk(chunk) == 0u) { failed = 1u; break; }
        for (slot = 0u; slot < V9X_GTT_CHUNK_DWORDS; ++slot) {
            DWORD raw = v9x_i9xx_gtt_chunk[slot];
            entry = (DWORD)chunk * V9X_GTT_CHUNK_DWORDS + slot;
            if (v9x_i9xx_gtt_inventory_add(&inventory, entry, raw) !=
                V9X_STATUS_OK) { failed = 1u; break; }
            if (entry == inventory.reserve_first_entry) { reserve_raw = raw; }
            if (run.count == 0ul) {
                v9x_start_run(&run, entry, raw);
            } else if (v9x_run_accepts(&run, raw) != 0u) {
                ++run.count;
                run.previous_physical = raw & V9X_I9XX_PTE_ADDRESS_MASK;
                run.previous_raw = raw;
            } else {
                v9x_write_run(&run, run_number++);
                v9x_start_run(&run, entry, raw);
            }
            v9x_i9xx_gtt_buffer[buffered++] = raw;
            if (buffered == V9X_GTT_BUFFER_DWORDS) {
                if (_lwrite(binary, (LPCSTR)v9x_i9xx_gtt_buffer,
                            sizeof(v9x_i9xx_gtt_buffer)) !=
                    sizeof(v9x_i9xx_gtt_buffer)) {
                    failed = 1u;
                    break;
                }
                buffered = 0u;
            }
        }
        if (failed != 0u) { break; }
    }
    if (_lclose(binary) == HFILE_ERROR) { failed = 1u; }
    if (failed != 0u || buffered != 0u ||
        v9x_i9xx_gtt_inventory_finish(
            &inventory, v9x_i9xx_gtt_hash_a, v9x_i9xx_gtt_hash_b) !=
            V9X_STATUS_OK) {
        WritePrivateProfileString("IntelGtt", "Result", "STREAM-FAILED",
                                  V9X_DIAG_INTELGTT_TXT);
        return;
    }
    if (run.count != 0ul) { v9x_write_run(&run, run_number++); }

    v9x_write_hex("Bar3", v9x_i9xx_gtt_bar3);
    v9x_write_hex("GmadrBar2", v9x_i9xx_gmadr_bar2);
    v9x_write_hex("Bsm", v9x_i9xx_bsm);
    v9x_write_hex("Ggc", v9x_i9xx_ggc);
    v9x_write_hex("StolenBytes", stolen);
    v9x_write_hex("VbeBytes", v9x_vbe_vram_reported);
    v9x_write_hex("PgtblCtl", v9x_i9xx_first[0]);
    v9x_write_hex("GttStorage", inventory.gtt_storage_physical);
    v9x_write_hex("Entries", inventory.expected_entries);
    v9x_write_hex("BinaryBytes", V9X_I9XX_GTT_BYTES);
    v9x_write_hex("HashA", inventory.hash_first);
    v9x_write_hex("HashB", inventory.hash_second);
    v9x_write_hex("HashStream", inventory.hash_stream);
    v9x_write_hex("Flags", inventory.flags);
    v9x_write_hex("Present", inventory.present_entries);
    v9x_write_hex("Uncached", inventory.uncached_entries);
    v9x_write_hex("Local", inventory.local_entries);
    v9x_write_hex("Cached", inventory.cached_entries);
    v9x_write_hex("UnknownAttrs", inventory.unknown_attribute_entries);
    v9x_write_hex("Runs", inventory.run_count);
    v9x_write_hex("RunsLogged", run_number);
    v9x_write_hex("BackedPrefix", inventory.backed_prefix_entries);
    v9x_write_hex("ReserveEntry", inventory.reserve_first_entry);
    v9x_write_hex("ReserveEntries", inventory.reserve_entry_count);
    v9x_write_hex("ReserveOffset", inventory.reserve_aperture_offset);
    v9x_write_hex("ReservePhysical", inventory.reserve_physical);
    /* These are the only GMADR reads in this phase.  Both offsets are within
     * the VBE-reported mapping, and each read is reached only after the PTE
     * decoded present and to the physical page expected from BSM. */
    /* The inventory above is written first so a refused sample still leaves
     * the whole table's evidence on disk; only the two data reads are withheld. */
    if ((inventory.first_raw & V9X_I9XX_PTE_VALID) == 0ul ||
        (inventory.first_raw & V9X_I9XX_PTE_ADDRESS_MASK) != inventory.bsm ||
        (reserve_raw & V9X_I9XX_PTE_VALID) == 0ul ||
        (reserve_raw & V9X_I9XX_PTE_ADDRESS_MASK) !=
            inventory.reserve_physical ||
        inventory.reserve_aperture_offset > v9x_vbe_vram_reported - 4ul) {
        v9x_write_hex("SampleCount", 0ul);
        WritePrivateProfileString("IntelGtt", "Result", "SAMPLE-REFUSED",
                                  V9X_DIAG_INTELGTT_TXT);
        return;
    }
    sample_zero = V9xGmadrRead(0ul);
    sample_reserve = V9xGmadrRead(inventory.reserve_aperture_offset);
    v9x_write_hex("SampleCount", 2ul);
    v9x_write_hex("Sample0Offset", 0ul);
    v9x_write_hex("Sample0Pte", inventory.first_raw);
    v9x_write_hex("Sample0Data", sample_zero);
    v9x_write_hex("SampleReserveOffset", inventory.reserve_aperture_offset);
    v9x_write_hex("SampleReservePte", reserve_raw);
    v9x_write_hex("SampleReserveData", sample_reserve);
    WritePrivateProfileString(
        "IntelGtt", "Result",
        (inventory.flags & V9X_I9XX_GTT_PHASE2_REQUIRED) ==
            V9X_I9XX_GTT_PHASE2_REQUIRED &&
        inventory.unknown_attribute_entries == 0ul &&
        run_number <= V9X_GTT_RUN_LOG_MAX ? "PASS" : "REVIEW",
        V9X_DIAG_INTELGTT_TXT);
}
