#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/hw16.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel16.h"

DWORD v9x_i9xx_event_value;
WORD v9x_i9xx_event_count;
WORD v9x_i9xx_event_dropped;

extern WORD FAR PASCAL V9xMiniI9xxEventCapture(WORD kind, WORD context);
extern WORD FAR PASCAL V9xMiniI9xxEventDword(WORD event, WORD field);
extern void FAR PASCAL V9xEnsureDiagDir(void);

static const char * const v9x_event_fields[V9X_I9XX_EVENT_DWORDS] = {
    "Sequence", "Kind", "Context", "Flags", "PgtblCtl", "RingTail",
    "RingHead", "RingStart", "RingCtl", "HwsPga", "Fence0", "Fence1",
    "Fence2", "Fence3", "Fence4", "Fence5", "Fence6", "Fence7",
    "GttHashA", "GttHashB"
};

static void v9x_event_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    short shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 0x0ful];
    }
    text[8] = '\0';
}

static void v9x_event_section(char *text, WORD event)
{
    static const char digits[] = "0123456789ABCDEF";
    static const char prefix[] = "IntelEvent";
    WORD index;
    for (index = 0u; index < (WORD)(sizeof(prefix) - 1u); ++index) {
        text[index] = prefix[index];
    }
    text[index++] = digits[(event >> 4) & 0x0fu];
    text[index++] = digits[event & 0x0fu];
    text[index] = '\0';
}

static const char *v9x_event_kind_text(WORD kind)
{
    switch (kind) {
    case V9X_HW16_EVENT_BOOT_ENABLE: return "boot-enable";
    case V9X_HW16_EVENT_ENABLE: return "enable";
    case V9X_HW16_EVENT_DISABLE: return "disable";
    case V9X_HW16_EVENT_MODE_SWITCH: return "mode-switch";
    case V9X_HW16_EVENT_MODE_RESTORE: return "mode-restore";
    case V9X_HW16_EVENT_DPMS: return "dpms";
    case 7u: return "ring-pre";
    case 8u: return "ring-post";
    default: return "unknown";
    }
}

static void v9x_event_write_hex(const char *section, const char *key,
                                DWORD value)
{
    char text[9];
    v9x_event_hex(text, value);
    WritePrivateProfileString(section, key, text, V9X_DIAG_INTELEVT_TXT);
}

void V9X_I9XX_FAR v9x_intel_publish_event(WORD kind, WORD context)
{
    DWORD record[V9X_I9XX_EVENT_DWORDS];
    DWORD coverage = 0ul;
    WORD event;
    WORD field;
    WORD all_safe = 1u;
    HFILE file;
    char section[13];

    V9xEnsureDiagDir();
    if (V9xMiniI9xxEventCapture(kind, context) == 0u ||
        v9x_i9xx_event_count == 0u ||
        v9x_i9xx_event_count > V9X_I9XX_EVENT_MAX) {
        WritePrivateProfileString("IntelEvents", "Result", "CAPTURE-FAILED",
                                  V9X_DIAG_INTELEVT_TXT);
        return;
    }

    /* Rebuild the text from the retained VxD journal. That journal includes
     * DPMS records captured at ring 0 since the previous display event. */
    /* Win9x caches the most recently used profile file in memory. Flush that
     * cache before truncating behind its back, or the previous boot's sections
     * can be merged back into the rewritten file; flush again afterwards so
     * the new contents are read from disk. */
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_INTELEVT_TXT);
    file = _lcreat(V9X_DIAG_INTELEVT_TXT, 0);
    if (file == HFILE_ERROR) { return; }
    _lclose(file);
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_INTELEVT_TXT);
    WritePrivateProfileString("IntelEvents", "Access", "read-only",
                              V9X_DIAG_INTELEVT_TXT);
    v9x_event_write_hex("IntelEvents", "Count", v9x_i9xx_event_count);
    v9x_event_write_hex("IntelEvents", "Dropped", v9x_i9xx_event_dropped);
    v9x_event_write_hex("IntelEvents", "RecordDwords",
                        V9X_I9XX_EVENT_DWORDS);

    for (event = 0u; event < v9x_i9xx_event_count; ++event) {
        v9x_event_section(section, event);
        for (field = 0u; field < V9X_I9XX_EVENT_DWORDS; ++field) {
            if (V9xMiniI9xxEventDword(event, field) == 0u) {
                WritePrivateProfileString("IntelEvents", "Result",
                                          "STREAM-FAILED",
                                          V9X_DIAG_INTELEVT_TXT);
                return;
            }
            record[field] = v9x_i9xx_event_value;
            v9x_event_write_hex(section, v9x_event_fields[field],
                                record[field]);
        }
        WritePrivateProfileString(
            section, "KindText",
            v9x_event_kind_text((WORD)record[V9X_I9XX_EVENT_KIND]),
            V9X_DIAG_INTELEVT_TXT);
        if ((record[V9X_I9XX_EVENT_FLAGS] & V9X_I9XX_EVENT_REQUIRED) !=
            V9X_I9XX_EVENT_REQUIRED) { all_safe = 0u; }
        switch ((WORD)record[V9X_I9XX_EVENT_KIND]) {
        case V9X_HW16_EVENT_BOOT_ENABLE: coverage |= 0x01ul; break;
        case V9X_HW16_EVENT_ENABLE: coverage |= 0x02ul; break;
        case V9X_HW16_EVENT_DISABLE: coverage |= 0x04ul; break;
        case V9X_HW16_EVENT_MODE_SWITCH: coverage |= 0x08ul; break;
        case V9X_HW16_EVENT_MODE_RESTORE: coverage |= 0x10ul; break;
        case V9X_HW16_EVENT_DPMS:
            coverage |= record[V9X_I9XX_EVENT_CONTEXT] == 0ul
                ? 0x20ul : 0x40ul;
            break;
        }
    }
    v9x_event_write_hex("IntelEvents", "Coverage", coverage);
    /* Measured 2026-09-12: a Display Properties resolution change is a
     * ReEnable rebuild (kind 4) with no Disable; nothing in a normal session
     * produces a second plain Enable (kind 2); and no DPMS record appears
     * because MiniVDD_GetMonitorPowerStateCaps advertises D0 only, so Windows
     * never requests a low-power state. Kind 5 (same-mode ReEnable) only
     * follows a full-screen DOS box, whose return hard-locks this machine
     * and is excluded from the matrix as a tier-0 display defect. READY
     * therefore asks for boot, disable and mode-switch. The other bits are
     * reported in Coverage for whoever extends the matrix. */
    WritePrivateProfileString(
        "IntelEvents", "Result",
        all_safe != 0u && v9x_i9xx_event_dropped == 0u &&
        (coverage & 0x0dul) == 0x0dul ? "READY" : "CAPTURED",
        V9X_DIAG_INTELEVT_TXT);
    /* Force the cached profile to disk now. A Disable record precedes a
     * VDD handoff that can hard-lock the machine, and a lazily flushed cache
     * would take the only evidence of it down too. */
    WritePrivateProfileString(0, 0, 0, V9X_DIAG_INTELEVT_TXT);
}
