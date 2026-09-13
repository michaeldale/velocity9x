#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include <string.h>
#include "velocity9x/build.h"
#include "velocity9x/intel_gma.h"

#define V9X_I9XX_BOOT_INI     "SYSTEM.INI"
#define V9X_I9XX_BOOT_SECTION "Velocity9x"

/* Never infer this from an on-disk key: it starts false on every driver load. */
WORD v9x_intel_boot_arm_latch;
DWORD v9x_intel_boot_arm_crc;
char v9x_intel_boot_arm_token[64];
static const char *v9x_intel_boot_state = "NOT-RUN";

const char *v9x_intel_boot_token_mover_state(void)
{
    return v9x_intel_boot_state;
}

static WORD v9x_intel_boot_read(const char *key, char *value, WORD capacity)
{
    WORD length;
    if (capacity < 2u) { return 0u; }
    value[0] = '\0';
    length = (WORD)GetPrivateProfileString(V9X_I9XX_BOOT_SECTION, key, "",
                                           value, capacity, V9X_I9XX_BOOT_INI);
    return length < capacity - 1u;
}

static WORD v9x_intel_boot_set(const char *key, const char *value)
{
    char check[96];
    if (!WritePrivateProfileString(V9X_I9XX_BOOT_SECTION, key, value,
                                   V9X_I9XX_BOOT_INI) ||
        !WritePrivateProfileString(0, 0, 0, V9X_I9XX_BOOT_INI) ||
        !v9x_intel_boot_read(key, check, sizeof(check))) {
        return 0u;
    }
    return strcmp(check, value) == 0;
}

/* Intel build only. Called from DriverInit before v9x_display_boot_log and
 * before any Enable. This transfers authority, but can reach no GPU write. */
void v9x_intel_boot_arm_prepare(void)
{
    char in_flight[65];
    char arm_once[65];
    char crc_text[16];
#ifdef V9X_I9XX_FIRST_WRITE_EXECUTOR
    char build_text[65];
#endif
    char last_result[96];
    DWORD crc = 0ul;

    v9x_intel_boot_arm_latch = 0u;
    v9x_intel_boot_arm_crc = 0ul;
    v9x_intel_boot_arm_token[0] = '\0';
    v9x_intel_boot_state = "IO-FAILED";
    if (!v9x_intel_boot_set("IntelEnableThisBoot", "0") ||
        !v9x_intel_boot_read("IntelInFlight", in_flight,
                             sizeof(in_flight))) {
        return;
    }
    if (in_flight[0] != '\0') {
        strcpy(last_result, "incomplete-reset:");
        strcat(last_result, in_flight);
        if (v9x_intel_boot_set("IntelLastResult", last_result)) {
            v9x_intel_boot_state = "INCOMPLETE";
        }
        return;
    }
    if (!v9x_intel_boot_read("IntelArmOnce", arm_once,
                             sizeof(arm_once))) {
        return;
    }
    if (arm_once[0] == '\0') {
        v9x_intel_boot_state = "READY";
        return;
    }
    if (v9x_i9xx_token_valid(arm_once) == V9X_FALSE) {
        v9x_intel_boot_state = "BAD-TOKEN";
        return;
    }
    if (!v9x_intel_boot_read("IntelArmCrc", crc_text,
                             sizeof(crc_text)) ||
        v9x_i9xx_parse_crc_hex(crc_text, &crc) == V9X_FALSE) {
        v9x_intel_boot_state = "BAD-CRC";
        return;
    }
#ifndef V9X_I9XX_FIRST_WRITE_EXECUTOR
    /* Never consume a one-shot token in a no-write-only build. The executor
     * and its Enable-side arm check must ship in the same binary. */
    v9x_intel_boot_state = "EXECUTOR-ABSENT";
    return;
#else
    if (!v9x_intel_boot_read("IntelArmBuildId", build_text,
                             sizeof(build_text)) ||
        strcmp(build_text, v9x_get_build_identity()->build_id) != 0) {
        v9x_intel_boot_state = "BAD-BUILD";
        return;
    }
    if (!v9x_intel_boot_set("IntelInFlight", arm_once) ||
        !v9x_intel_boot_set("IntelArmOnce", "") ||
        !v9x_intel_boot_set("IntelEnableThisBoot", "1")) {
        return;
    }
    strcpy(v9x_intel_boot_arm_token, arm_once);
    v9x_intel_boot_arm_crc = crc;
    v9x_intel_boot_arm_latch = 1u;
    v9x_intel_boot_state = "ARMED";
#endif
}
