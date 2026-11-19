/*
 * Trinity (the A10's HD 7660D): the engine clock the VBIOS allows at its
 * boot voltage, set at engine validation.
 *
 * The VBIOS boots M-D7TFW4's engine at 200 MHz (MMIO 600h, divider ID 46h)
 * and the BIOS leaves the SMU's DPM unprogrammed, so nothing raises it.
 * Its PowerPlay lists 304 MHz at the same VDDC index; set by hand that took
 * 3DMark 2001 SE from 2462 to 2605
 * (docs\decisions\2026-11-19-trinity-engine-clock-200mhz.md).
 *
 * What to set is tn_clocks.c's policy, from the APU's own PowerPlay table;
 * how is the VBIOS's own SetEngineClock, run by the AtomBIOS interpreter
 * (src\common\atombios.c) against the register window, as si_clock_apply.c
 * runs the Oland's. On Trinity that table waits for 604h bit 0, writes the
 * divider ID to 600h and waits again; it sets no voltage. The clock is read
 * first through GetEngineClock, and a clock already at its target is not
 * set again, so a mode change's revalidation leaves it alone.
 *
 * The outcome goes to C:\V9XDIAG\V9XCLK.TXT whenever this runs.
 */
#include "ddhal_internal.h"
#include "velocity9x/atombios.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/tn_clocks.h"

#define V9X_TN_CLK_LOG_PATH V9X_DIAG_PATH("V9XCLK.TXT")

/* The video BIOS the system BIOS shadowed at C0000h, as si_clock_apply.c
 * finds the Oland's: linear C0000h in every Win32 process on Win98, its
 * length from the option ROM header, its device from the PCI data
 * structure. */
#define V9X_TN_CLK_ROM_LINEAR   0x000c0000ul
#define V9X_TN_CLK_ROM_BLOCK    512ul
#define V9X_TN_CLK_PCIR_POINTER 0x18ul
#define V9X_TN_CLK_PCIR         0x52494350ul
#define V9X_TN_CLK_VENDOR_ATI   0x1002ul
#define V9X_TN_CLK_DEVICE       0x9901ul

/* The register window is BAR2's 128 KiB (eng_eg.c's V9X_EG_MMIO_BYTES). */
#define V9X_TN_CLK_MMIO_BYTES   0x00020000ul

/* A delay is reads of SRBM_STATUS, four a microsecond at least, as
 * si_clock_apply.c's. */
#define V9X_TN_CLK_SRBM_STATUS  0x00000e50ul
#define V9X_TN_CLK_READS_PER_US 4ul

/* A clock within 0.1 MHz of its target is at it. */
#define V9X_TN_CLK_SLACK 10ul

/* The interpreter and its parameter space: static, past the HAL's
 * stack-frame limit, and serialised by the Win16 lock. */
static struct v9x_atom v9x_tn_clk_atom;
static v9x_u32 v9x_tn_clk_params[16];
static int v9x_tn_clk_bad_offset;

/* The last validation that set the clock, kept for as long as the HAL is
 * loaded: a later revalidation finds it set and rewrites the record as
 * already-set (M-D7TFW4 boot 78). */
static const char *v9x_tn_clk_last_set = "none";
static DWORD v9x_tn_clk_last_set_before;
static DWORD v9x_tn_clk_last_set_after;

static v9x_u32 v9x_tn_clk_read(void *context, v9x_u32 offset)
{
    (void)context;
    if (offset >= V9X_TN_CLK_MMIO_BYTES) {
        v9x_tn_clk_bad_offset = 1;
        return 0ul;
    }
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               offset);
}

static void v9x_tn_clk_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    (void)context;
    if (offset >= V9X_TN_CLK_MMIO_BYTES) {
        v9x_tn_clk_bad_offset = 1;
        return;
    }
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static void v9x_tn_clk_delay(void *context, v9x_u32 microseconds)
{
    DWORD reads = microseconds * V9X_TN_CLK_READS_PER_US;

    (void)context;
    while (reads-- != 0ul) {
        (void)v9x_tn_clk_read(0, V9X_TN_CLK_SRBM_STATUS);
    }
}

static const struct v9x_atom_io v9x_tn_clk_io = {
    0, v9x_tn_clk_read, v9x_tn_clk_write, v9x_tn_clk_delay
};

/* --- the record ---------------------------------------------------------- */

static void v9x_tn_clk_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_tn_clk_number(HANDLE file, const char *key, DWORD value)
{
    char digits[12];
    int count = 0;

    v9x_tn_clk_text(file, key);
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < 11);
    while (count != 0) {
        char one[2];

        one[0] = digits[--count];
        one[1] = '\0';
        v9x_tn_clk_text(file, one);
    }
    v9x_tn_clk_text(file, "\r\n");
}

static void v9x_tn_clk_record(const char *result, v9x_status plan_status,
                              const struct v9x_tn_clock_plan *plan,
                              DWORD before, v9x_status set_status,
                              DWORD after)
{
    HANDLE file;

    file = CreateFileA(V9X_TN_CLK_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_tn_clk_text(file, "[TrinityClocks]\r\nResult=");
    v9x_tn_clk_text(file, result);
    v9x_tn_clk_text(file, "\r\n");
    v9x_tn_clk_number(file, "PlanStatus=", plan_status);
    v9x_tn_clk_number(file, "DefaultSclk10kHz=", plan->default_sclk);
    v9x_tn_clk_number(file, "BootVddcIndex=", plan->boot_vddc_index);
    v9x_tn_clk_number(file, "Levels=", plan->levels);
    v9x_tn_clk_number(file, "LevelsAtBootVddc=", plan->levels_allowed);
    v9x_tn_clk_number(file, "TargetSclk10kHz=", plan->sclk);
    v9x_tn_clk_number(file, "BeforeSclk10kHz=", before);
    v9x_tn_clk_number(file, "SetSclkStatus=", set_status);
    v9x_tn_clk_number(file, "AfterSclk10kHz=", after);
    v9x_tn_clk_number(file, "FaultOpcode=", v9x_tn_clk_atom.fault_opcode);
    v9x_tn_clk_number(file, "FaultOffset=", v9x_tn_clk_atom.fault_offset);
    v9x_tn_clk_text(file, "LastSet=");
    v9x_tn_clk_text(file, v9x_tn_clk_last_set);
    v9x_tn_clk_text(file, "\r\n");
    v9x_tn_clk_number(file, "LastSetBefore10kHz=", v9x_tn_clk_last_set_before);
    v9x_tn_clk_number(file, "LastSetAfter10kHz=", v9x_tn_clk_last_set_after);
    CloseHandle(file);
}

/* --- the VBIOS ----------------------------------------------------------- */

/* The shadowed image, if it is readable, an option ROM, and the PCI data
 * structure names Trinity. 0 otherwise. */
static const v9x_u8 *v9x_tn_clk_rom(DWORD *bytes)
{
    const v9x_u8 *rom = (const v9x_u8 *)V9X_TN_CLK_ROM_LINEAR;
    DWORD pcir;
    DWORD length;

    if (IsBadReadPtr(rom, V9X_TN_CLK_ROM_BLOCK) || rom[0] != 0x55u ||
        rom[1] != 0xaau) {
        return 0;
    }
    length = (DWORD)rom[2] * V9X_TN_CLK_ROM_BLOCK;
    if (length < V9X_TN_CLK_ROM_BLOCK || IsBadReadPtr(rom, length)) {
        return 0;
    }
    pcir = (DWORD)rom[V9X_TN_CLK_PCIR_POINTER] |
           ((DWORD)rom[V9X_TN_CLK_PCIR_POINTER + 1ul] << 8);
    if (pcir + 8ul > length ||
        *(const DWORD *)(rom + pcir) != V9X_TN_CLK_PCIR ||
        ((DWORD)rom[pcir + 4ul] | ((DWORD)rom[pcir + 5ul] << 8)) !=
            V9X_TN_CLK_VENDOR_ATI ||
        ((DWORD)rom[pcir + 6ul] | ((DWORD)rom[pcir + 7ul] << 8)) !=
            V9X_TN_CLK_DEVICE) {
        return 0;
    }
    *bytes = length;
    return rom;
}

/* GetEngineClock's answer in 10 kHz units, 0 on failure. */
static DWORD v9x_tn_clk_get(void)
{
    DWORD index;

    for (index = 0ul; index < 16ul; ++index) {
        v9x_tn_clk_params[index] = 0ul;
    }
    if (v9x_atom_execute(&v9x_tn_clk_atom, V9X_ATOM_CMD_GET_ENGINE_CLOCK,
                         v9x_tn_clk_params, 16ul) != V9X_STATUS_OK) {
        return 0ul;
    }
    return v9x_tn_clk_params[0] & 0x00fffffful;
}

static int v9x_tn_clk_at(DWORD clock, DWORD target)
{
    return clock + V9X_TN_CLK_SLACK >= target &&
           clock <= target + V9X_TN_CLK_SLACK;
}

int v9x_tn_clocks_apply(void)
{
    struct v9x_tn_clock_plan plan;
    const v9x_u8 *rom;
    DWORD rom_bytes = 0ul;
    DWORD before;
    DWORD after;
    DWORD index;
    v9x_status plan_status;
    v9x_status set_status = V9X_STATUS_OK;

    plan.default_sclk = 0ul;
    plan.boot_vddc_index = 0ul;
    plan.levels = 0ul;
    plan.levels_allowed = 0ul;
    plan.sclk = 0ul;
    v9x_tn_clk_bad_offset = 0;

    rom = v9x_tn_clk_rom(&rom_bytes);
    if (rom == 0 || v9x_atom_init(&v9x_tn_clk_atom, rom, rom_bytes,
                                  &v9x_tn_clk_io) != V9X_STATUS_OK) {
        v9x_tn_clk_record("no-vbios", V9X_STATUS_OK, &plan, 0ul,
                          V9X_STATUS_OK, 0ul);
        return 1;
    }
    plan_status = v9x_tn_clocks_plan(&v9x_tn_clk_atom, &plan);
    before = v9x_tn_clk_get();
    if (plan_status != V9X_STATUS_OK || before == 0ul) {
        v9x_tn_clk_record("not-understood", plan_status, &plan, before,
                          V9X_STATUS_OK, before);
        return 1;
    }
    if (v9x_tn_clk_at(before, plan.sclk)) {
        v9x_tn_clk_record("already-set", plan_status, &plan, before,
                          V9X_STATUS_OK, before);
        return 1;
    }

    for (index = 0ul; index < 16ul; ++index) {
        v9x_tn_clk_params[index] = 0ul;
    }
    v9x_tn_clk_params[0] = plan.sclk;
    set_status = v9x_atom_execute(&v9x_tn_clk_atom,
                                  V9X_ATOM_CMD_SET_ENGINE_CLOCK,
                                  v9x_tn_clk_params, 16ul);
    after = v9x_tn_clk_get();
    v9x_tn_clk_last_set_before = before;
    v9x_tn_clk_last_set_after = after;
    if (set_status != V9X_STATUS_OK || v9x_tn_clk_bad_offset) {
        /* The table stopped part way: the engine is not claimed on it. */
        v9x_tn_clk_last_set = "set-failed";
        v9x_tn_clk_record("set-failed", plan_status, &plan, before,
                          set_status, after);
        return 0;
    }
    v9x_tn_clk_last_set = v9x_tn_clk_at(after, plan.sclk) ? "set"
                                                          : "set-not-read-back";
    v9x_tn_clk_record(v9x_tn_clk_last_set, plan_status, &plan, before,
                      set_status, after);
    return 1;
}
