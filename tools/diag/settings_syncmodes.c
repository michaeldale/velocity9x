/*
 * V9xSyncModes: mirror the driver's validated mode inventory into the native
 * Display Properties registry tree.
 *
 * GDI validates a dynamic row the moment modes16.c commits it, but the native
 * Windows 9x Settings page enumerates the display class key's MODES tree,
 * which the INF wrote once at install time. This entry point bridges that
 * boot-time gap: it reads C:\V9XDIAG\V9XMODES.INI - written by the 16-bit driver
 * after a successful enable - validates it whole, and creates the missing
 * MODES\<depth>\<width>,<height> keys for published rows, stamping only the
 * keys it creates so it can later prune only what it owns.
 *
 * The INF runs this through Run (not RunOnce - the inventory changes whenever
 * the card, panel or BIOS-visible mode set changes, so it re-runs every boot
 * and is idempotent when nothing changed):
 *
 *     rundll32.exe v9xsetp.dll,V9xSyncModes
 *
 * Passing the word "report" in the rundll32 argument string is the dry run:
 * every intended operation is written to the report file and the registry is
 * not touched.
 *
 * Safety rules, in the order they can stop a run:
 *
 *   - an absent, torn (Complete!=1), unknown-schema, malformed, duplicated or
 *     out-of-range inventory changes nothing and says why in the report;
 *   - the target is the display class instance the INF marked with this
 *     family's V9xFamily value; zero or more than one marked instance is a
 *     recorded no-op, and so is a marked instance no hardware devnode under
 *     HKLM\Enum currently points at;
 *   - only keys stamped V9xDynamic=1 are ever deleted, and only when a
 *     complete current inventory does not publish their geometry. INF
 *     baseline keys, MODES\4, and anything unstamped are never touched;
 *   - a hidden inventory row is neither created nor pruned: it means "do not
 *     add a dynamic key for this geometry", never "delete the baseline key".
 *
 * Everything here is registry and profile API only: the DLL links without a
 * C runtime, so parsing is hand-rolled and formatting is wsprintfA.
 */
#include <windows.h>

#include "velocity9x/diagpaths.h"

#define V9X_INVENTORY_PATH    V9X_DIAG_MODES_INI
#define V9X_INVENTORY_SECTION "Velocity9xModes"
#define V9X_SYNC_REPORT_PATH  V9X_DIAG_SYNC_INI
#define V9X_SYNC_SECTION      "Velocity9xSync"
#define V9X_DISPLAY_CLASS_KEY \
    "System\\CurrentControlSet\\Services\\Class\\Display"

#define V9X_SYNC_MAX_ROWS 64u

struct v9x_sync_row {
    WORD width;
    WORD height;
    WORD depth;
};

static struct v9x_sync_row v9x_rows[V9X_SYNC_MAX_ROWS];
static WORD v9x_row_count;
static char v9x_family[32];
static DWORD v9x_generation;
static WORD v9x_dry_run;
static WORD v9x_op_count;

static void v9x_sync_write(const char *key, const char *value)
{
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SYNC_SECTION, key, value,
                               V9X_SYNC_REPORT_PATH);
}

static void v9x_sync_op(const char *action, const char *detail)
{
    char key[16];
    char text[128];

    wsprintfA(key, "Op%02u", (unsigned int)v9x_op_count++);
    wsprintfA(text, "%s %s", action, detail);
    v9x_sync_write(key, text);
}

static void v9x_sync_fail(const char *reason)
{
    v9x_sync_write("Status", "no-op");
    v9x_sync_write("Reason", reason);
}

/* Unsigned decimal at *text; advances the cursor. Returns FALSE when the
 * first character is not a digit or the value leaves 16 bits. */
static BOOL v9x_parse_u16(const char **text, WORD *value)
{
    const char *at = *text;
    DWORD out = 0ul;

    if (*at < '0' || *at > '9') {
        return FALSE;
    }
    while (*at >= '0' && *at <= '9') {
        out = out * 10ul + (DWORD)(*at - '0');
        if (out > 0xfffful) {
            return FALSE;
        }
        ++at;
    }
    *text = at;
    *value = (WORD)out;
    return TRUE;
}

/* The named unsigned field out of a "name=value name=value" line. */
static BOOL v9x_line_field(const char *line, const char *name, WORD *value)
{
    const char *at = line;
    int length = lstrlenA(name);

    while (*at != '\0') {
        if (at == line || at[-1] == ' ') {
            int index;
            BOOL match = TRUE;

            for (index = 0; index < length; ++index) {
                if (at[index] != name[index]) {
                    match = FALSE;
                    break;
                }
            }
            if (match && at[length] == '=') {
                const char *cursor = at + length + 1;

                return v9x_parse_u16(&cursor, value);
            }
        }
        ++at;
    }
    return FALSE;
}

/* "g=WxHxB" out of one RowNN inventory line. */
static BOOL v9x_parse_row(const char *line, struct v9x_sync_row *row)
{
    const char *at = line;

    while (*at != '\0') {
        if ((at == line || at[-1] == ' ') && at[0] == 'g' && at[1] == '=') {
            const char *cursor = at + 2;

            if (!v9x_parse_u16(&cursor, &row->width) || *cursor != 'x') {
                return FALSE;
            }
            ++cursor;
            if (!v9x_parse_u16(&cursor, &row->height) || *cursor != 'x') {
                return FALSE;
            }
            ++cursor;
            if (!v9x_parse_u16(&cursor, &row->depth)) {
                return FALSE;
            }
            return TRUE;
        }
        ++at;
    }
    return FALSE;
}

static BOOL v9x_row_known(WORD width, WORD height, WORD depth)
{
    WORD index;

    for (index = 0u; index < v9x_row_count; ++index) {
        if (v9x_rows[index].width == width &&
            v9x_rows[index].height == height &&
            v9x_rows[index].depth == depth) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * Read and validate the whole inventory. TRUE only when every rule passes;
 * on FALSE the failure reason is already in the report and nothing may
 * proceed to the registry.
 */
static BOOL v9x_load_inventory(void)
{
    char line[160];
    char key[16];
    char table_line[96];
    WORD rows = 0u;
    WORD published = 0u;
    WORD index;
    static const char digits[] = "0123456789abcdef";

    if (GetPrivateProfileIntA(V9X_INVENTORY_SECTION, "Complete", 0,
                              V9X_INVENTORY_PATH) != 1) {
        v9x_sync_fail("inventory-missing-or-incomplete");
        return FALSE;
    }
    if (GetPrivateProfileIntA(V9X_INVENTORY_SECTION, "Schema", 0,
                              V9X_INVENTORY_PATH) != 1) {
        v9x_sync_fail("inventory-unknown-schema");
        return FALSE;
    }
    v9x_family[0] = '\0';
    GetPrivateProfileStringA(V9X_INVENTORY_SECTION, "Family", "",
                             v9x_family, sizeof(v9x_family),
                             V9X_INVENTORY_PATH);
    if (v9x_family[0] == '\0') {
        v9x_sync_fail("inventory-no-family");
        return FALSE;
    }
    v9x_generation = (DWORD)GetPrivateProfileIntA(
        V9X_INVENTORY_SECTION, "Generation", 0, V9X_INVENTORY_PATH);
    if (v9x_generation == 0ul) {
        v9x_sync_fail("inventory-no-generation");
        return FALSE;
    }

    table_line[0] = '\0';
    GetPrivateProfileStringA(V9X_INVENTORY_SECTION, "Table", "",
                             table_line, sizeof(table_line),
                             V9X_INVENTORY_PATH);
    if (!v9x_line_field(table_line, "rows", &rows) ||
        !v9x_line_field(table_line, "published", &published) ||
        rows == 0u || rows > (WORD)V9X_SYNC_MAX_ROWS ||
        published == 0u || published > rows) {
        v9x_sync_fail("inventory-counts-out-of-range");
        return FALSE;
    }

    v9x_row_count = 0u;
    for (index = 0u; index < rows; ++index) {
        BOOL have_row;
        BOOL have_hidden;
        char hidden_key[16];

        key[0] = 'R'; key[1] = 'o'; key[2] = 'w';
        key[3] = digits[(index >> 4) & 0x0fu];
        key[4] = digits[index & 0x0fu];
        key[5] = '\0';
        hidden_key[0] = 'H'; hidden_key[1] = 'i'; hidden_key[2] = 'd';
        hidden_key[3] = 'd'; hidden_key[4] = 'e'; hidden_key[5] = 'n';
        hidden_key[6] = digits[(index >> 4) & 0x0fu];
        hidden_key[7] = digits[index & 0x0fu];
        hidden_key[8] = '\0';

        line[0] = '\0';
        GetPrivateProfileStringA(V9X_INVENTORY_SECTION, key, "",
                                 line, sizeof(line), V9X_INVENTORY_PATH);
        have_row = line[0] != '\0';
        if (have_row) {
            struct v9x_sync_row row;

            if (!v9x_parse_row(line, &row) ||
                row.width == 0u || row.width > 4095u ||
                row.height == 0u || row.height > 4095u ||
                (row.depth != 8u && row.depth != 16u && row.depth != 32u)) {
                v9x_sync_fail("inventory-malformed-row");
                return FALSE;
            }
            if (v9x_row_known(row.width, row.height, row.depth)) {
                v9x_sync_fail("inventory-duplicate-row");
                return FALSE;
            }
            v9x_rows[v9x_row_count++] = row;
        }

        line[0] = '\0';
        GetPrivateProfileStringA(V9X_INVENTORY_SECTION, hidden_key, "",
                                 line, sizeof(line), V9X_INVENTORY_PATH);
        have_hidden = line[0] != '\0';
        /* Every table index is exactly one of published or hidden; anything
         * else is a torn or hand-edited file. */
        if (have_row == have_hidden) {
            v9x_sync_fail("inventory-row-coverage");
            return FALSE;
        }
    }
    if (v9x_row_count != published) {
        v9x_sync_fail("inventory-published-count-disagrees");
        return FALSE;
    }
    return TRUE;
}

/*
 * The one display class instance the INF marked for this family, as its
 * four-digit subkey name. FALSE - already reported - on zero or several.
 */
static BOOL v9x_find_marked_instance(char *instance, DWORD instance_size)
{
    HKEY display;
    DWORD index = 0ul;
    WORD matches = 0u;
    char name[16];

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, V9X_DISPLAY_CLASS_KEY, 0,
                      KEY_READ, &display) != ERROR_SUCCESS) {
        v9x_sync_fail("no-display-class-key");
        return FALSE;
    }
    for (;;) {
        char family[32];
        DWORD size = sizeof(name);
        DWORD type;
        DWORD family_size = sizeof(family);
        HKEY candidate;

        if (RegEnumKeyExA(display, index++, name, &size, 0, 0, 0, 0) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(display, name, 0, KEY_READ, &candidate) !=
            ERROR_SUCCESS) {
            continue;
        }
        family[0] = '\0';
        if (RegQueryValueExA(candidate, "V9xFamily", 0, &type,
                             (BYTE *)family, &family_size) == ERROR_SUCCESS &&
            type == REG_SZ && lstrcmpiA(family, v9x_family) == 0) {
            ++matches;
            if (matches == 1u && (DWORD)lstrlenA(name) < instance_size) {
                lstrcpyA(instance, name);
            }
        }
        RegCloseKey(candidate);
    }
    RegCloseKey(display);

    if (matches == 0u) {
        v9x_sync_fail("no-marked-instance");
        return FALSE;
    }
    if (matches > 1u) {
        v9x_sync_fail("multiple-marked-instances");
        return FALSE;
    }
    return TRUE;
}

/*
 * Does any hardware devnode under HKLM\Enum currently point at this class
 * instance? A marked class key whose devnode is gone - the card was removed,
 * or the driver was moved to another instance - must not be written to.
 */
static BOOL v9x_instance_has_devnode(const char *instance)
{
    char wanted[32];
    HKEY enum_root;
    DWORD bus_index = 0ul;
    BOOL found = FALSE;

    wsprintfA(wanted, "Display\\%s", instance);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ, &enum_root) !=
        ERROR_SUCCESS) {
        return FALSE;
    }
    /* Enum is three levels deep: enumerator \ device \ instance. */
    for (; !found;) {
        char bus[64];
        DWORD size = sizeof(bus);
        HKEY bus_key;
        DWORD device_index = 0ul;

        if (RegEnumKeyExA(enum_root, bus_index++, bus, &size, 0, 0, 0, 0) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(enum_root, bus, 0, KEY_READ, &bus_key) !=
            ERROR_SUCCESS) {
            continue;
        }
        for (; !found;) {
            char device[128];
            HKEY device_key;
            DWORD node_index = 0ul;

            size = sizeof(device);
            if (RegEnumKeyExA(bus_key, device_index++, device, &size,
                              0, 0, 0, 0) != ERROR_SUCCESS) {
                break;
            }
            if (RegOpenKeyExA(bus_key, device, 0, KEY_READ, &device_key) !=
                ERROR_SUCCESS) {
                continue;
            }
            for (; !found;) {
                char node[128];
                HKEY node_key;
                char driver[32];
                DWORD type;
                DWORD driver_size = sizeof(driver);

                size = sizeof(node);
                if (RegEnumKeyExA(device_key, node_index++, node, &size,
                                  0, 0, 0, 0) != ERROR_SUCCESS) {
                    break;
                }
                if (RegOpenKeyExA(device_key, node, 0, KEY_READ, &node_key) !=
                    ERROR_SUCCESS) {
                    continue;
                }
                driver[0] = '\0';
                if (RegQueryValueExA(node_key, "Driver", 0, &type,
                                     (BYTE *)driver, &driver_size) ==
                        ERROR_SUCCESS &&
                    type == REG_SZ && lstrcmpiA(driver, wanted) == 0) {
                    found = TRUE;
                }
                RegCloseKey(node_key);
            }
            RegCloseKey(device_key);
        }
        RegCloseKey(bus_key);
    }
    RegCloseKey(enum_root);
    return found;
}

/* Create the missing MODES keys for published rows; stamp only what this
 * run creates. */
static BOOL v9x_sync_add_rows(HKEY class_key)
{
    WORD index;
    char path[48];
    char detail[48];

    for (index = 0u; index < v9x_row_count; ++index) {
        const struct v9x_sync_row *row = &v9x_rows[index];
        HKEY mode_key;
        DWORD disposition = 0ul;

        wsprintfA(path, "MODES\\%u\\%u,%u", (unsigned int)row->depth,
                  (unsigned int)row->width, (unsigned int)row->height);
        wsprintfA(detail, "%ux%ux%u", (unsigned int)row->width,
                  (unsigned int)row->height, (unsigned int)row->depth);
        if (v9x_dry_run != 0u) {
            HKEY probe;

            if (RegOpenKeyExA(class_key, path, 0, KEY_READ, &probe) ==
                ERROR_SUCCESS) {
                RegCloseKey(probe);
                v9x_sync_op("keep", detail);
            } else {
                v9x_sync_op("add", detail);
            }
            continue;
        }
        if (RegCreateKeyExA(class_key, path, 0, 0, 0, KEY_READ | KEY_WRITE,
                            0, &mode_key, &disposition) != ERROR_SUCCESS) {
            v9x_sync_op("add-failed", detail);
            return FALSE;
        }
        if (disposition == REG_CREATED_NEW_KEY) {
            char generation[16];

            wsprintfA(generation, "%lu", v9x_generation);
            /* The default value is the refresh list, matching the INF's
             * baseline keys; 60 is the same convention the driver reports. */
            RegSetValueExA(mode_key, 0, 0, REG_SZ, (const BYTE *)"60",
                           3ul);
            RegSetValueExA(mode_key, "V9xDynamic", 0, REG_SZ,
                           (const BYTE *)"1", 2ul);
            RegSetValueExA(mode_key, "V9xGeneration", 0, REG_SZ,
                           (const BYTE *)generation,
                           (DWORD)lstrlenA(generation) + 1ul);
            v9x_sync_op("add", detail);
        } else {
            v9x_sync_op("keep", detail);
        }
        RegCloseKey(mode_key);
    }
    return TRUE;
}

/* "width,height" subkey name back into numbers. */
static BOOL v9x_parse_geometry_key(const char *name, WORD *width,
                                   WORD *height)
{
    const char *cursor = name;

    if (!v9x_parse_u16(&cursor, width) || *cursor != ',') {
        return FALSE;
    }
    ++cursor;
    if (!v9x_parse_u16(&cursor, height) || *cursor != '\0') {
        return FALSE;
    }
    return TRUE;
}

/* Delete stamped dynamic keys whose geometry the current inventory no longer
 * publishes. Only V9xDynamic-stamped keys, only under the depths this driver
 * publishes - MODES\4 and unstamped keys are structurally out of reach. */
static void v9x_sync_prune(HKEY class_key)
{
    static const WORD depths[] = { 8u, 16u, 32u };
    WORD depth_index;

    for (depth_index = 0u; depth_index < 3u; ++depth_index) {
        char depth_path[16];
        HKEY depth_key;
        char doomed[V9X_SYNC_MAX_ROWS][12];
        WORD doomed_count = 0u;
        DWORD index = 0ul;
        WORD victim;

        wsprintfA(depth_path, "MODES\\%u", (unsigned int)depths[depth_index]);
        if (RegOpenKeyExA(class_key, depth_path, 0, KEY_READ | KEY_WRITE,
                          &depth_key) != ERROR_SUCCESS) {
            continue;
        }
        for (;;) {
            char name[32];
            DWORD size = sizeof(name);
            HKEY mode_key;
            char stamp[8];
            DWORD stamp_size = sizeof(stamp);
            DWORD type;
            WORD width;
            WORD height;

            if (RegEnumKeyExA(depth_key, index++, name, &size, 0, 0, 0, 0) !=
                ERROR_SUCCESS) {
                break;
            }
            if (!v9x_parse_geometry_key(name, &width, &height)) {
                continue;
            }
            if (RegOpenKeyExA(depth_key, name, 0, KEY_READ, &mode_key) !=
                ERROR_SUCCESS) {
                continue;
            }
            stamp[0] = '\0';
            if (RegQueryValueExA(mode_key, "V9xDynamic", 0, &type,
                                 (BYTE *)stamp, &stamp_size) ==
                    ERROR_SUCCESS &&
                type == REG_SZ && lstrcmpA(stamp, "1") == 0 &&
                !v9x_row_known(width, height, depths[depth_index]) &&
                doomed_count < (WORD)V9X_SYNC_MAX_ROWS &&
                (DWORD)lstrlenA(name) < sizeof(doomed[0])) {
                lstrcpyA(doomed[doomed_count++], name);
            }
            RegCloseKey(mode_key);
        }
        for (victim = 0u; victim < doomed_count; ++victim) {
            char detail[32];

            wsprintfA(detail, "%s\\%s", depth_path, doomed[victim]);
            if (v9x_dry_run != 0u) {
                v9x_sync_op("delete", detail);
            } else if (RegDeleteKeyA(depth_key, doomed[victim]) ==
                       ERROR_SUCCESS) {
                v9x_sync_op("delete", detail);
            } else {
                v9x_sync_op("delete-failed", detail);
            }
        }
        RegCloseKey(depth_key);
    }
}

/* DEFAULT\Mode names "depth,width,height". If it names a geometry the
 * inventory no longer publishes, repoint it at a published row of the same
 * depth - inventory order puts the family baseline first, which is the
 * documented preference until EDID exists - or the first published row. */
static void v9x_sync_default_mode(HKEY class_key)
{
    HKEY default_key;
    char value[32];
    DWORD size = sizeof(value);
    DWORD type;
    const char *cursor;
    WORD depth;
    WORD width;
    WORD height;
    WORD index;
    const struct v9x_sync_row *replacement = 0;
    REGSAM access = v9x_dry_run != 0u ? KEY_READ
                                      : (KEY_READ | KEY_WRITE);

    if (RegOpenKeyExA(class_key, "DEFAULT", 0, access, &default_key) !=
        ERROR_SUCCESS) {
        return;
    }
    value[0] = '\0';
    if (RegQueryValueExA(default_key, "Mode", 0, &type, (BYTE *)value,
                         &size) != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(default_key);
        return;
    }
    cursor = value;
    if (!v9x_parse_u16(&cursor, &depth) || *cursor != ',') {
        RegCloseKey(default_key);
        return;
    }
    ++cursor;
    if (!v9x_parse_u16(&cursor, &width) || *cursor != ',') {
        RegCloseKey(default_key);
        return;
    }
    ++cursor;
    if (!v9x_parse_u16(&cursor, &height)) {
        RegCloseKey(default_key);
        return;
    }
    /* 4 bpp is the permanent VGA fallback and is not this tool's business. */
    if (depth == 4u || v9x_row_known(width, height, depth)) {
        RegCloseKey(default_key);
        return;
    }
    for (index = 0u; index < v9x_row_count; ++index) {
        if (v9x_rows[index].depth == depth) {
            replacement = &v9x_rows[index];
            break;
        }
    }
    if (replacement == 0) {
        replacement = &v9x_rows[0];
    }
    wsprintfA(value, "%u,%u,%u", (unsigned int)replacement->depth,
              (unsigned int)replacement->width,
              (unsigned int)replacement->height);
    if (v9x_dry_run == 0u) {
        RegSetValueExA(default_key, "Mode", 0, REG_SZ, (const BYTE *)value,
                       (DWORD)lstrlenA(value) + 1ul);
    }
    v9x_sync_op("default-mode", value);
    RegCloseKey(default_key);
}

void CALLBACK V9xSyncModes(HWND owner, HINSTANCE instance, LPSTR command,
                           int show)
{
    char instance_name[16];
    char class_path[80];
    char generation[16];
    HKEY class_key;
    REGSAM access;

    (void)owner;
    (void)instance;
    (void)show;

    /* Start every run with a fresh report: a stale "ok" over a failing boot
     * is worse than no report at all. */
    WritePrivateProfileStringA(V9X_SYNC_SECTION, 0, 0, V9X_SYNC_REPORT_PATH);
    v9x_op_count = 0u;
    v9x_dry_run = 0u;
    if (command != 0) {
        const char *at = command;

        while (*at != '\0') {
            if ((at[0] == 'r' || at[0] == 'R') &&
                (at[1] == 'e' || at[1] == 'E') &&
                (at[2] == 'p' || at[2] == 'P')) {
                v9x_dry_run = 1u;
                break;
            }
            ++at;
        }
    }
    v9x_sync_write("Build", V9X_BUILD_ID);
    v9x_sync_write("DryRun", v9x_dry_run != 0u ? "1" : "0");

    if (!v9x_load_inventory()) {
        return;
    }
    if (!v9x_find_marked_instance(instance_name, sizeof(instance_name))) {
        return;
    }
    if (!v9x_instance_has_devnode(instance_name)) {
        v9x_sync_fail("marked-instance-has-no-devnode");
        return;
    }
    v9x_sync_write("Instance", instance_name);

    wsprintfA(class_path, "%s\\%s", V9X_DISPLAY_CLASS_KEY, instance_name);
    access = v9x_dry_run != 0u ? KEY_READ : (KEY_READ | KEY_WRITE);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, class_path, 0, access,
                      &class_key) != ERROR_SUCCESS) {
        v9x_sync_fail("class-instance-open-failed");
        return;
    }

    if (!v9x_sync_add_rows(class_key)) {
        RegCloseKey(class_key);
        v9x_sync_fail("registry-write-failed");
        return;
    }
    v9x_sync_prune(class_key);
    v9x_sync_default_mode(class_key);

    wsprintfA(generation, "%lu", v9x_generation);
    if (v9x_dry_run == 0u) {
        RegSetValueExA(class_key, "V9xSyncGeneration", 0, REG_SZ,
                       (const BYTE *)generation,
                       (DWORD)lstrlenA(generation) + 1ul);
    }
    RegCloseKey(class_key);
    v9x_sync_write("Generation", generation);
    v9x_sync_write("Status", "ok");
}
