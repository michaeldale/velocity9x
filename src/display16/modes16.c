/*
 * The runtime mode table: what GDI is offered, as opposed to what the family
 * ships.
 *
 * v9x_hw16.modes remains the immutable family baseline. This module owns a
 * copy of it in DGROUP, merged - when the mini-VDD's boot-time VBE collection
 * produced a valid cache - with the modes the BIOS actually reports, through
 * the host-tested judgement in src\common\vbe_modes.c. Initialization is
 * transactional: the baseline copy is committed first and stands whole
 * whenever any later step refuses, so a family without collection, a missing
 * mini-VDD, an invalid scan or a defect all land on exactly the static table
 * the driver shipped with, every row published.
 *
 * Publication is the second half: a baseline row a trustworthy scan
 * contradicts keeps its storage slot and is offered to nothing. The rule
 * itself lives in v9x_vbe_publish_rows; this module only supplies the
 * trustworthiness verdict from the mini-VDD status bits.
 */
#include <windows.h>

#include "velocity9x/hw16.h"
#include "velocity9x/vbe_modes.h"
#include "velocity9x/vbe_cache.h"

/* The mini-VDD API v2 scalars and thunks, owned by enable16.c/runtime.asm. */
extern DWORD v9x_minivdd_base;
extern WORD v9x_minivdd_bytes;
extern WORD v9x_minivdd_lin_bytes;
extern WORD v9x_minivdd_attr;
extern WORD v9x_minivdd_mode_number;
extern WORD v9x_minivdd_width;
extern WORD v9x_minivdd_height;
extern WORD v9x_minivdd_bpp;
extern WORD v9x_minivdd_significant;
extern WORD v9x_minivdd_model;
extern WORD v9x_minivdd_record_flags;
extern WORD v9x_minivdd_red;
extern WORD v9x_minivdd_green;
extern WORD v9x_minivdd_blue;
extern WORD v9x_minivdd_rsvd;
extern WORD v9x_minivdd_total64k;
extern WORD v9x_minivdd_cached;
extern WORD v9x_minivdd_status;
extern WORD FAR PASCAL V9xMiniVbeController(void);
extern WORD FAR PASCAL V9xMiniVbeStatus(void);
extern WORD FAR PASCAL V9xMiniVbeModeAt(WORD index);
extern WORD FAR PASCAL V9xMiniVbeModeMasks(WORD index);

/*
 * The committed table. ddi.c's v9x_modes/V9X_MODE_COUNT macros point here, so
 * every GDI lookup, ValidateMode, Enable/ReEnable selection and fallback path
 * reads the same rows, the same count and the same publication flags.
 *
 * Fixed DGROUP storage, deliberately: 64 rows is 896 bytes of V9X_HW16_MODE,
 * 768 of masks and 64 publication bytes, inside the audited 32 KiB budget.
 */
V9X_HW16_MODE v9x_runtime_modes[V9X_MODE_TABLE_MAX];
struct v9x_mode_masks v9x_runtime_masks[V9X_MODE_TABLE_MAX];
v9x_u8 v9x_runtime_publication[V9X_MODE_TABLE_MAX];
WORD v9x_runtime_count = 0u;
WORD v9x_runtime_published = 0u;
/* Index of the first published row: the fallback row wherever the design
 * used to say "baseline row zero". Equal to zero except on a machine whose
 * scan contradicts row zero itself. */
WORD v9x_runtime_first = 0u;
/* Diagnostics for the inventory and the settings report. */
WORD v9x_runtime_dropped = 0u;
WORD v9x_runtime_reasons[V9X_VBE_ADMIT_REASON_COUNT];
/* How far the dynamic path got:
 *   0 baseline - no API answered (non-scanning family, or no mini-VDD);
 *   1 merged, scan trusted for hiding;
 *   2 merged, scan valid but not trusted for hiding (truncated/overflowed);
 *   3 baseline - scan invalid (controller or list validity missing);
 *   4 baseline - record read or commit validation refused. */
WORD v9x_runtime_scan_state = 0u;
/* VRAM as 4F00h reported it, for the inventory's raw figure. */
DWORD v9x_runtime_vram_reported = 0ul;

/* Bounded staging, so a failure mid-build never touches the committed table.
 * Static rather than stack: the Win16 stack cannot hold 64 rows. */
static struct v9x_vbe_scan_entry v9x_scan_entries[V9X_VBE_CACHE_MAX];
static V9X_HW16_MODE v9x_stage_modes[V9X_MODE_TABLE_MAX];
static struct v9x_mode_masks v9x_stage_masks[V9X_MODE_TABLE_MAX];
static v9x_u8 v9x_stage_publication[V9X_MODE_TABLE_MAX];
static WORD v9x_scan_count = 0u;

/* Copy the family baseline in and publish every row: the committed fallback
 * state, and the whole story for a build whose scan never happens. */
static void v9x_modes16_commit_baseline(void)
{
    WORD index;
    WORD count = v9x_hw16.mode_count;

    if (count > V9X_MODE_TABLE_MAX) {
        count = V9X_MODE_TABLE_MAX;
    }
    for (index = 0u; index < count; ++index) {
        v9x_runtime_modes[index] = v9x_hw16.modes[index];
        if (v9x_hw16.modes[index].bits_per_pixel == 16u) {
            v9x_runtime_masks[index].red = 0x0000f800ul;
            v9x_runtime_masks[index].green = 0x000007e0ul;
            v9x_runtime_masks[index].blue = 0x0000001ful;
        } else if (v9x_hw16.modes[index].bits_per_pixel == 24u ||
                   v9x_hw16.modes[index].bits_per_pixel == 32u) {
            v9x_runtime_masks[index].red = 0x00ff0000ul;
            v9x_runtime_masks[index].green = 0x0000ff00ul;
            v9x_runtime_masks[index].blue = 0x000000fful;
        } else {
            v9x_runtime_masks[index].red = 0ul;
            v9x_runtime_masks[index].green = 0ul;
            v9x_runtime_masks[index].blue = 0ul;
        }
        v9x_runtime_publication[index] = V9X_MODE_PUB_PUBLISHED;
    }
    v9x_runtime_count = count;
    v9x_runtime_published = count;
    v9x_runtime_first = 0u;
}

/* One cached record into a scan entry, from the same two API calls the
 * Stage 1 dump used. The mask words pack size in the low byte and position in
 * the high one. */
static WORD v9x_modes16_read_record(WORD index,
                                    struct v9x_vbe_scan_entry *entry)
{
    struct v9x_vbe_mode_summary *summary = &entry->summary;

    if (V9xMiniVbeModeAt(index) == 0u || V9xMiniVbeModeMasks(index) == 0u) {
        return 0u;
    }
    entry->mode_number = v9x_minivdd_mode_number;
    v9x_vbe_mode_summary_clear(summary);
    summary->attributes = v9x_minivdd_attr;
    summary->bytes_per_scan_line = v9x_minivdd_bytes;
    summary->lin_bytes_per_scan_line = v9x_minivdd_lin_bytes;
    summary->width = v9x_minivdd_width;
    summary->height = v9x_minivdd_height;
    summary->bits_per_pixel = v9x_minivdd_bpp;
    summary->significant_depth = v9x_minivdd_significant;
    summary->mask_flags = v9x_minivdd_record_flags;
    summary->memory_model = v9x_minivdd_model;
    summary->phys_base = v9x_minivdd_base;
    summary->red_mask_size = (v9x_u16)(v9x_minivdd_red & 0x00ffu);
    summary->red_field_position = (v9x_u16)(v9x_minivdd_red >> 8);
    summary->green_mask_size = (v9x_u16)(v9x_minivdd_green & 0x00ffu);
    summary->green_field_position = (v9x_u16)(v9x_minivdd_green >> 8);
    summary->blue_mask_size = (v9x_u16)(v9x_minivdd_blue & 0x00ffu);
    summary->blue_field_position = (v9x_u16)(v9x_minivdd_blue >> 8);
    summary->rsvd_mask_size = (v9x_u16)(v9x_minivdd_rsvd & 0x00ffu);
    summary->rsvd_field_position = (v9x_u16)(v9x_minivdd_rsvd >> 8);
    return 1u;
}

/* A committed row must describe a surface; a zero anywhere here would be a
 * builder defect, and the transaction refuses rather than offering it. */
static WORD v9x_modes16_rows_sane(const V9X_HW16_MODE *table, WORD count)
{
    WORD index;

    for (index = 0u; index < count; ++index) {
        if (table[index].width == 0u || table[index].height == 0u ||
            table[index].bits_per_pixel == 0u || table[index].pitch == 0u ||
            table[index].vbe_mode == 0u) {
            return 0u;
        }
    }
    return 1u;
}

void v9x_modes16_init(void)
{
    WORD index;
    WORD count;
    WORD trusted;
    WORD first = 0u;
    WORD published;
    DWORD vram;

    /* Step 1: the fallback state is committed before anything can fail. */
    v9x_modes16_commit_baseline();
    v9x_scan_count = 0u;
    v9x_runtime_dropped = 0u;
    for (index = 0u; index < V9X_VBE_ADMIT_REASON_COUNT; ++index) {
        v9x_runtime_reasons[index] = 0u;
    }

    /* Step 2: the mini-VDD's verdict on its own collection. */
    if (V9xMiniVbeStatus() == 0u) {
        v9x_runtime_scan_state = 0u;
        return;
    }
    if ((v9x_minivdd_status & (V9X_VBE_ST_CTRL_VALID |
                               V9X_VBE_ST_LIST_VALID)) !=
        (V9X_VBE_ST_CTRL_VALID | V9X_VBE_ST_LIST_VALID)) {
        v9x_runtime_scan_state = 3u;
        return;
    }
    if (V9xMiniVbeController() == 0u) {
        v9x_runtime_scan_state = 3u;
        return;
    }
    vram = (DWORD)v9x_minivdd_total64k * 65536ul;
    v9x_runtime_vram_reported = vram;

    /* Step 3: at most V9X_VBE_CACHE_MAX records into bounded storage. */
    count = v9x_minivdd_cached;
    if (count > V9X_VBE_CACHE_MAX) {
        count = V9X_VBE_CACHE_MAX;
    }
    for (index = 0u; index < count; ++index) {
        if (v9x_modes16_read_record(index,
                                    &v9x_scan_entries[index]) == 0u) {
            v9x_runtime_scan_state = 4u;
            return;
        }
    }
    v9x_scan_count = count;

    /* Steps 4 and 5: build into staging, then refuse anything malformed. */
    count = v9x_vbe_build_mode_table_ex(
        v9x_hw16.modes, v9x_hw16.mode_count,
        v9x_scan_entries, v9x_scan_count, vram, 0,
        v9x_stage_modes, v9x_stage_masks, V9X_MODE_TABLE_MAX,
        &v9x_runtime_dropped, v9x_runtime_reasons);
    if (count < v9x_hw16.mode_count || count > V9X_MODE_TABLE_MAX ||
        v9x_modes16_rows_sane(v9x_stage_modes, count) == 0u) {
        v9x_runtime_scan_state = 4u;
        return;
    }

    /*
     * Step 6: publication. Hiding a baseline row needs a scan that is
     * complete and whole: a properly terminated list, and no truncation
     * anywhere - a full cache or a hit query limit means modes exist that
     * were never examined, and one of them could be the row's.
     */
    trusted = ((v9x_minivdd_status & V9X_VBE_ST_LIST_TERM) != 0u &&
               (v9x_minivdd_status &
                (V9X_VBE_ST_LIST_OVERFLOW | V9X_VBE_ST_LIST_FLAGGED |
                 V9X_VBE_ST_LIST_UNREACHED | V9X_VBE_ST_CACHE_FULL |
                 V9X_VBE_ST_QUERY_FAILED | V9X_VBE_ST_QUERY_LIMIT)) == 0u)
                  ? V9X_TRUE
                  : V9X_FALSE;
    published = v9x_vbe_publish_rows(v9x_stage_modes, count,
                                     v9x_hw16.mode_count,
                                     v9x_scan_entries, v9x_scan_count, vram,
                                     trusted, v9x_stage_publication, &first);
    if (published == 0u || first >= count) {
        v9x_runtime_scan_state = 4u;
        return;
    }

    /* Step 7: commit, all together and only now. */
    for (index = 0u; index < count; ++index) {
        v9x_runtime_modes[index] = v9x_stage_modes[index];
        v9x_runtime_masks[index] = v9x_stage_masks[index];
        v9x_runtime_publication[index] = v9x_stage_publication[index];
    }
    v9x_runtime_count = count;
    v9x_runtime_published = published;
    v9x_runtime_first = first;
    v9x_runtime_scan_state = trusted == V9X_TRUE ? 1u : 2u;
}

WORD v9x_modes16_is_published(WORD index)
{
    if (index >= v9x_runtime_count) {
        return 0u;
    }
    return (v9x_runtime_publication[index] & V9X_MODE_PUB_PUBLISHED) != 0u
               ? 1u
               : 0u;
}

/*
 * The validated mode inventory: what a registry synchronizer (Stage 4) or a
 * person reads to learn what this boot's runtime table holds and why.
 *
 * Written after a successful enable, with the Complete sentinel discipline
 * the plan requires: Complete=0 goes down before any row is replaced and
 * Complete=1 is the last write, so a reader never mistakes a torn file for a
 * whole one.
 */
#define V9X_MODE_INVENTORY_PATH "C:\\V9XMODES.INI"
#define V9X_MODE_INVENTORY_SECTION "Velocity9xModes"
#define V9X_MODE_INVENTORY_SCHEMA "1"

extern WORD v9x_minivdd_version;
extern DWORD v9x_minivdd_capabilities;
extern WORD v9x_minivdd_oem_revision;
extern WORD v9x_minivdd_listed;
extern WORD v9x_minivdd_queried;
/* enable16.c: VRAM after the visible-surface deduction - the usable figure,
 * beside the raw one this module recorded. */
extern DWORD v9x_vbe_vram_bytes;

static const char v9x_inv_digits[] = "0123456789abcdef";

static WORD v9x_inv_decimal(char *text, WORD at, DWORD value)
{
    char scratch[10];
    WORD used = 0u;

    do {
        scratch[used++] = v9x_inv_digits[(WORD)(value % 10ul)];
        value /= 10ul;
    } while (value != 0ul && used < 10u);
    while (used != 0u) {
        text[at++] = scratch[--used];
    }
    return at;
}

static WORD v9x_inv_hex16(char *text, WORD at, WORD value)
{
    text[at++] = v9x_inv_digits[(value >> 12) & 0x0fu];
    text[at++] = v9x_inv_digits[(value >> 8) & 0x0fu];
    text[at++] = v9x_inv_digits[(value >> 4) & 0x0fu];
    text[at++] = v9x_inv_digits[value & 0x0fu];
    return at;
}

static WORD v9x_inv_hex32(char *text, WORD at, DWORD value)
{
    at = v9x_inv_hex16(text, at, (WORD)(value >> 16));
    return v9x_inv_hex16(text, at, (WORD)(value & 0xffffu));
}

static WORD v9x_inv_literal(char *text, WORD at, const char *literal)
{
    while (*literal != '\0') {
        text[at++] = *literal++;
    }
    return at;
}

static void v9x_inv_write(const char *key, const char *value)
{
    WritePrivateProfileString(V9X_MODE_INVENTORY_SECTION, key, value,
                              V9X_MODE_INVENTORY_PATH);
}

/* RowNN / HiddenNN key names, two hex digits like the boot dump's VbeModeNN. */
static void v9x_inv_row_key(char *key, const char *prefix, WORD index)
{
    WORD at = 0u;

    at = v9x_inv_literal(key, at, prefix);
    key[at++] = v9x_inv_digits[(index >> 4) & 0x0fu];
    key[at++] = v9x_inv_digits[index & 0x0fu];
    key[at] = '\0';
}

void v9x_modes16_write_inventory(void)
{
    static char text[128];
    static char key[12];
    WORD at;
    WORD index;
    WORD generation;

    /* Torn-file discipline: invalidate, replace, then validate. */
    generation = (WORD)(GetPrivateProfileInt(V9X_MODE_INVENTORY_SECTION,
                                             "Generation", 0,
                                             V9X_MODE_INVENTORY_PATH) + 1u);
    v9x_inv_write("Complete", "0");

    /* Stale rows from a boot with a longer table must not survive. */
    for (index = 0u; index < V9X_MODE_TABLE_MAX; ++index) {
        v9x_inv_row_key(key, "Row", index);
        v9x_inv_write(key, 0);
        v9x_inv_row_key(key, "Hidden", index);
        v9x_inv_write(key, 0);
    }

    v9x_inv_write("Schema", V9X_MODE_INVENTORY_SCHEMA);
    v9x_inv_write("Build", V9X_BUILD_ID);
    v9x_inv_write("Family", v9x_hw16.family_id);

    /* BIOS identity, or an explicit unavailable: without it every other line
     * is attributed to a chip when the evidence says the BIOS is the
     * variable. */
    if (v9x_runtime_scan_state == 1u || v9x_runtime_scan_state == 2u) {
        at = 0u;
        at = v9x_inv_literal(text, at, "v=");
        at = v9x_inv_hex16(text, at, v9x_minivdd_version);
        at = v9x_inv_literal(text, at, " caps=");
        at = v9x_inv_hex32(text, at, v9x_minivdd_capabilities);
        at = v9x_inv_literal(text, at, " rev=");
        at = v9x_inv_hex16(text, at, v9x_minivdd_oem_revision);
        text[at] = '\0';
        v9x_inv_write("Bios", text);
    } else {
        v9x_inv_write("Bios", "unavailable");
    }

    /* The raw and usable VRAM figures, separately, so a wrong report is
     * identifiable rather than merely fatal. */
    at = 0u;
    at = v9x_inv_literal(text, at, "reported=");
    at = v9x_inv_decimal(text, at, v9x_runtime_vram_reported);
    at = v9x_inv_literal(text, at, " usable=");
    at = v9x_inv_decimal(text, at, v9x_vbe_vram_bytes);
    text[at] = '\0';
    v9x_inv_write("Vram", text);

    /* The published rate is the 60 Hz convention, not a measurement, and the
     * file says so rather than leaving the number to be believed. */
    v9x_inv_write("RefreshRate", "convention-60");

    at = 0u;
    at = v9x_inv_literal(text, at, "state=");
    at = v9x_inv_decimal(text, at, v9x_runtime_scan_state);
    at = v9x_inv_literal(text, at, " listed=");
    at = v9x_inv_decimal(text, at, v9x_minivdd_listed);
    at = v9x_inv_literal(text, at, " queried=");
    at = v9x_inv_decimal(text, at, v9x_minivdd_queried);
    at = v9x_inv_literal(text, at, " cached=");
    at = v9x_inv_decimal(text, at, v9x_scan_count);
    at = v9x_inv_literal(text, at, " flags=");
    at = v9x_inv_hex16(text, at, v9x_minivdd_status);
    text[at] = '\0';
    v9x_inv_write("Scan", text);

    at = 0u;
    at = v9x_inv_literal(text, at, "rows=");
    at = v9x_inv_decimal(text, at, v9x_runtime_count);
    at = v9x_inv_literal(text, at, " published=");
    at = v9x_inv_decimal(text, at, v9x_runtime_published);
    at = v9x_inv_literal(text, at, " first=");
    at = v9x_inv_decimal(text, at, v9x_runtime_first);
    at = v9x_inv_literal(text, at, " dropped=");
    at = v9x_inv_decimal(text, at, v9x_runtime_dropped);
    text[at] = '\0';
    v9x_inv_write("Table", text);

    /* One rejection tally per admission reason, in reason order. */
    at = 0u;
    for (index = 0u; index < V9X_VBE_ADMIT_REASON_COUNT; ++index) {
        if (index != 0u) {
            text[at++] = ',';
        }
        at = v9x_inv_decimal(text, at, v9x_runtime_reasons[index]);
    }
    text[at] = '\0';
    v9x_inv_write("Reasons", text);

    for (index = 0u; index < v9x_runtime_count; ++index) {
        const V9X_HW16_MODE *row = &v9x_runtime_modes[index];
        WORD baseline = index < v9x_hw16.mode_count ? 1u : 0u;

        at = 0u;
        at = v9x_inv_literal(text, at, "m=");
        at = v9x_inv_hex16(text, at, row->vbe_mode);
        at = v9x_inv_literal(text, at, " g=");
        at = v9x_inv_decimal(text, at, row->width);
        text[at++] = 'x';
        at = v9x_inv_decimal(text, at, row->height);
        text[at++] = 'x';
        at = v9x_inv_decimal(text, at, row->bits_per_pixel);
        at = v9x_inv_literal(text, at, " p=");
        at = v9x_inv_decimal(text, at, row->pitch);
        at = v9x_inv_literal(text, at, " rgb=");
        at = v9x_inv_hex32(text, at, v9x_runtime_masks[index].red);
        text[at++] = ',';
        at = v9x_inv_hex32(text, at, v9x_runtime_masks[index].green);
        text[at++] = ',';
        at = v9x_inv_hex32(text, at, v9x_runtime_masks[index].blue);
        at = v9x_inv_literal(text, at,
                             baseline != 0u ? " src=baseline" : " src=dynamic");
        text[at] = '\0';

        if (v9x_modes16_is_published(index) != 0u) {
            v9x_inv_row_key(key, "Row", index);
            v9x_inv_write(key, text);
        } else {
            /* A hidden row is an entry with a reason, never a mode. */
            at = v9x_inv_literal(text, at, " hide=scan-contradicted");
            text[at] = '\0';
            v9x_inv_row_key(key, "Hidden", index);
            v9x_inv_write(key, text);
        }
    }

    at = 0u;
    at = v9x_inv_decimal(text, at, generation);
    text[at] = '\0';
    v9x_inv_write("Generation", text);
    v9x_inv_write("Complete", "1");
}
