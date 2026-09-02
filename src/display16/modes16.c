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

#include "velocity9x/diagpaths.h"
#include "velocity9x/hw16.h"
#include "velocity9x/vbe_modes.h"
#include "velocity9x/vbe_cache.h"
#include "velocity9x/edid.h"

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
extern WORD FAR PASCAL V9xMiniVbeEdidChunk(WORD index);
extern DWORD v9x_minivdd_edid0;
extern DWORD v9x_minivdd_edid1;
extern DWORD v9x_minivdd_edid2;
extern DWORD v9x_minivdd_edid3;

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
/* The panel's parsed EDID preference: non-zero when the mini-VDD collected a
 * valid block 0 whose preferred timing this parser accepts. A hint only - it
 * never adds a mode, removes one, or overrides a valid user selection. */
WORD v9x_edid_state = 0u;
static struct v9x_edid_summary v9x_edid;

/* Bounded staging, so a failure mid-build never touches the committed table.
 * Static rather than stack: the Win16 stack cannot hold 64 rows. */
static struct v9x_vbe_scan_entry v9x_scan_entries[V9X_VBE_CACHE_MAX];
static V9X_HW16_MODE v9x_stage_modes[V9X_MODE_TABLE_MAX];
static struct v9x_mode_masks v9x_stage_masks[V9X_MODE_TABLE_MAX];
static v9x_u8 v9x_stage_publication[V9X_MODE_TABLE_MAX];
static WORD v9x_scan_count = 0u;

/*
 * What a 16 bpp desktop means on this machine.
 *
 * Two readings, because the answer needs a fact that arrives late. The
 * SYSTEM.INI key is read at mode-table init so an explicit 15 or 16 is
 * honoured from the first row published. Automatic - the key absent - depends
 * on what Direct3D resolves to, and that needs the chip, which the PCI scan
 * establishes only at Enable. So v9x_modes16_resolve_layout runs again there,
 * before the first mode set and before the DIB engine is told anything, and
 * re-stamps the 16 bpp rows. Between the two readings automatic means 5:6:5,
 * which is what an unresolved machine had before any of this existed.
 *
 * The same file, section and macro names as dd16.c and the two settings
 * surfaces, and check-tree.ps1 asserts they agree.
 *
 * See V9X_HIGHCOLOR_555 for why 5:5:5 is worth having at all, and
 * v9x_highcolor_resolve for the rule.
 */
#define V9X_SETTINGS_SECTION "Velocity9x"
#define V9X_SETTINGS_INI     "SYSTEM.INI"
#define V9X_HIGHCOLOR_KEY    "HighColor"

static WORD v9x_highcolor_setting = (WORD)V9X_HIGHCOLOR_AUTO;
static WORD v9x_highcolor = (WORD)V9X_HIGHCOLOR_565;

WORD v9x_modes16_highcolor(void)
{
    return v9x_highcolor;
}

WORD v9x_modes16_highcolor_setting(void)
{
    return v9x_highcolor_setting;
}

/* "555" or "565", followed by how it was decided, for V9XHW.INI and the
 * Display Properties page. Stable text: settings_status.c reads it back. */
const char *v9x_modes16_layout_text(void)
{
    if (v9x_highcolor == (WORD)V9X_HIGHCOLOR_555) {
        return v9x_highcolor_setting == (WORD)V9X_HIGHCOLOR_555
            ? "555-ini" : "555-auto";
    }
    return v9x_highcolor_setting == (WORD)V9X_HIGHCOLOR_565
        ? "565-ini" : "565-auto";
}

static void v9x_modes16_read_highcolor(void)
{
    v9x_highcolor_setting = (WORD)GetPrivateProfileInt(V9X_SETTINGS_SECTION,
                                                       V9X_HIGHCOLOR_KEY,
                                                       (int)V9X_HIGHCOLOR_AUTO,
                                                       V9X_SETTINGS_INI);
}

/* Non-zero when this row should be published, and programmed, as 5:5:5. Both
 * conditions in one place: the setting asks for it, and the row's mode number
 * has a 15 bpp sibling to switch to. A row with no sibling stays 5:6:5 rather
 * than being described as something the mode set cannot deliver. */
WORD v9x_modes16_row_is_555(const V9X_HW16_MODE *row)
{
    if (row == 0 || v9x_highcolor != (WORD)V9X_HIGHCOLOR_555) {
        return 0u;
    }
    if (row->bits_per_pixel != 16u) {
        return 0u;
    }
    return v9x_vbe_mode_555(row->vbe_mode) != 0u ? 1u : 0u;
}

/*
 * Decide the layout against the resolved Direct3D state, and make the
 * published masks say so.
 *
 * Called from the Enable path once the chip is known, and before
 * v9x_apply_mode is re-run for the row being enabled - the caller does that,
 * because this file does not own the active mode. Every 16 bpp row with a
 * 15 bpp sibling is re-stamped, not only the rows whose mask changed: a row
 * the BIOS scan described as 5:6:5 has to become 5:5:5 too, since the mode set
 * will program the sibling regardless of what the BIOS said about the 16 bpp
 * one, and a layout that flips between boots must leave no stale mask behind.
 * A 16 bpp row with no sibling keeps whatever the scan or the baseline gave
 * it.
 */
void v9x_modes16_resolve_layout(WORD d3d_state)
{
    WORD index;

    v9x_modes16_read_highcolor();
    v9x_highcolor = (WORD)v9x_highcolor_resolve(v9x_highcolor_setting,
                                                d3d_state);
    for (index = 0u; index < v9x_runtime_count; ++index) {
        const V9X_HW16_MODE *row = &v9x_runtime_modes[index];

        if (row->bits_per_pixel != 16u ||
            v9x_vbe_mode_555(row->vbe_mode) == 0u) {
            continue;
        }
        if (v9x_modes16_row_is_555(row) != 0u) {
            v9x_mode_masks_555(&v9x_runtime_masks[index]);
        } else {
            v9x_runtime_masks[index].red = 0x0000f800ul;
            v9x_runtime_masks[index].green = 0x000007e0ul;
            v9x_runtime_masks[index].blue = 0x0000001ful;
        }
    }
}

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
        if (v9x_modes16_row_is_555(&v9x_hw16.modes[index]) != 0u) {
            v9x_mode_masks_555(&v9x_runtime_masks[index]);
        } else if (v9x_hw16.modes[index].bits_per_pixel == 16u) {
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

/* Read and parse the mini-VDD's cached EDID block. Runs after the table
 * commit and cannot affect it: DDC failure is non-fatal by design, and a
 * refusal here only clears the preference. */
static void v9x_modes16_read_edid(void)
{
    static v9x_u8 block[V9X_EDID_BLOCK_BYTES];
    WORD chunk;

    v9x_edid_state = 0u;
    for (chunk = 0u; chunk < (WORD)(V9X_EDID_BLOCK_BYTES / 16u); ++chunk) {
        DWORD *slice = (DWORD *)&block[chunk * 16u];

        if (V9xMiniVbeEdidChunk(chunk) == 0u) {
            return;
        }
        slice[0] = v9x_minivdd_edid0;
        slice[1] = v9x_minivdd_edid1;
        slice[2] = v9x_minivdd_edid2;
        slice[3] = v9x_minivdd_edid3;
    }
    if (v9x_edid_parse(block, &v9x_edid) == V9X_TRUE) {
        v9x_edid_state = 1u;
    }
}

void v9x_modes16_init(void)
{
    WORD index;
    WORD count;
    WORD trusted;
    WORD first = 0u;
    WORD published;
    DWORD vram;

    /* The layout setting first: the baseline commit below publishes masks
     * derived from it. The chip is not known yet, so automatic resolves to
     * 5:6:5 here and is decided properly at Enable - see
     * v9x_modes16_resolve_layout. */
    v9x_modes16_read_highcolor();
    v9x_highcolor = (WORD)v9x_highcolor_resolve(v9x_highcolor_setting,
                                                V9X_D3D_STATE_NONE);

    /* Step 1: the fallback state is committed before anything can fail. */
    v9x_modes16_commit_baseline();
    v9x_modes16_read_edid();
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
 * The published row matching the panel's EDID-preferred geometry at the
 * requested storage depth, or null. This is the fallback half of the
 * selection order - configured mode first, this second, first published row
 * last - so it can only ever be consulted after the configured mode failed
 * to resolve, and a monitor change can never override a valid selection.
 */
const V9X_HW16_MODE *v9x_modes16_edid_mode(WORD bits_per_pixel)
{
    WORD index;

    if (v9x_edid_state == 0u) {
        return 0;
    }
    for (index = 0u; index < v9x_runtime_count; ++index) {
        if (v9x_modes16_is_published(index) != 0u &&
            v9x_runtime_modes[index].width == v9x_edid.preferred_width &&
            v9x_runtime_modes[index].height == v9x_edid.preferred_height &&
            v9x_runtime_modes[index].bits_per_pixel == bits_per_pixel) {
            return &v9x_runtime_modes[index];
        }
    }
    return 0;
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
#define V9X_MODE_INVENTORY_PATH V9X_DIAG_MODES_INI
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

/* runtime.asm: create V9X_DIAG_DIR once before the first diagnostic write. */
extern void FAR PASCAL V9xEnsureDiagDir(void);

static void v9x_inv_write(const char *key, const char *value)
{
    V9xEnsureDiagDir();
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

    /* The panel's preference and what this driver recommends from it: a
     * report, never an action. The recommendation names a published geometry
     * or says why there is none. */
    if (v9x_edid_state != 0u) {
        WORD match = 0u;

        at = 0u;
        at = v9x_inv_literal(text, at, "v=");
        at = v9x_inv_hex16(text, at, v9x_edid.version);
        at = v9x_inv_literal(text, at, " preferred=");
        at = v9x_inv_decimal(text, at, v9x_edid.preferred_width);
        text[at++] = 'x';
        at = v9x_inv_decimal(text, at, v9x_edid.preferred_height);
        at = v9x_inv_literal(text, at, " ext=");
        at = v9x_inv_decimal(text, at, v9x_edid.extension_count);
        text[at] = '\0';
        v9x_inv_write("Edid", text);

        for (index = 0u; index < v9x_runtime_count; ++index) {
            if (v9x_modes16_is_published(index) != 0u &&
                v9x_runtime_modes[index].width == v9x_edid.preferred_width &&
                v9x_runtime_modes[index].height ==
                    v9x_edid.preferred_height) {
                match = 1u;
                break;
            }
        }
        at = 0u;
        at = v9x_inv_decimal(text, at, v9x_edid.preferred_width);
        text[at++] = 'x';
        at = v9x_inv_decimal(text, at, v9x_edid.preferred_height);
        at = v9x_inv_literal(text, at, match != 0u
                                           ? " reason=edid-preferred"
                                           : " reason=edid-unpublished");
        text[at] = '\0';
        v9x_inv_write("Recommendation", text);
    } else {
        v9x_inv_write("Edid", "none");
        v9x_inv_write("Recommendation", "none reason=no-edid");
    }

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
            /* A hidden row is an entry with a reason, never a mode.
             *
             * The mechanism is always the scan (v9x_vbe_publish_rows hides a
             * baseline row no admitted record describes), but when EDID says
             * the panel is smaller than the row, the cause is knowable and
             * the file should say it: on a machine read only after the fact,
             * "the panel cannot show this" explains itself where
             * "the scan lacked it" invites a BIOS hunt. */
            if (v9x_edid_state != 0u &&
                (row->width > v9x_edid.preferred_width ||
                 row->height > v9x_edid.preferred_height)) {
                at = v9x_inv_literal(text, at, " hide=edid-contradicted");
            } else {
                at = v9x_inv_literal(text, at, " hide=scan-contradicted");
            }
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
