/*
 * Oland (GCN 1): the engine and memory clocks the VBIOS allows, set at
 * engine validation (docs\plans\ati-si-clocks-from-the-vbios.md).
 *
 * The BIOS leaves W9QI0DAD's Oland at 300 MHz engine and 300 MHz DDR3, of a
 * rated 1050 and 1000, with no power management running; setting 500 and
 * 1000 took 3DMark 2001 SE from 4234 to 4860
 * (docs\decisions\2026-11-18-oland-clocks-left-by-the-bios.md).
 *
 * What to set is si_clocks.c's policy, from the card's own PowerPlay table;
 * how is the card's own SetEngineClock and SetMemoryClock, run by the
 * AtomBIOS interpreter (src\common\atombios.c) straight against the
 * register window, as Linux runs them: with interrupts on, polls bounded by
 * the interpreter's step limit. Here the HAL holds the Win16 lock, so GDI
 * does not draw while SetMemoryClock has the CPU's VRAM access off; a
 * software cursor at interrupt time could lose a frame's writes, and with
 * BIF_FB_EN off nothing waits on VRAM, so nothing hangs.
 *
 * Only before the graphics CP has started: the tables black the memory
 * clients out, but a running CP is not something this has measured. The
 * clocks are read first through GetEngineClock and GetMemoryClock (which
 * only read, so they are reported under a running CP too), and a clock
 * already at its target is not set again, so a mode change's revalidation
 * leaves them alone.
 *
 * The outcome goes to C:\V9XDIAG\V9XCLK.TXT whenever this runs.
 */
#include "ddhal_internal.h"
#include "velocity9x/atombios.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/si_clocks.h"

#define V9X_SI_CLK_LOG_PATH V9X_DIAG_PATH("V9XCLK.TXT")

/* The video BIOS the system BIOS shadowed at C0000h, mapped at linear
 * C0000h in every Win32 process on Win98 (the first megabyte of the system
 * VM); its option ROM header gives its length in 512-byte blocks. */
#define V9X_SI_CLK_ROM_LINEAR 0x000c0000ul
#define V9X_SI_CLK_ROM_BLOCK  512ul
/* The PCI data structure's offset at 18h: "PCIR", vendor, device. */
#define V9X_SI_CLK_PCIR_POINTER 0x18ul
#define V9X_SI_CLK_PCIR         0x52494350ul
#define V9X_SI_CLK_VENDOR_ATI   0x1002ul

/* The register window is 256 KiB (V9X_VI_MMIO_BYTES). */
#define V9X_SI_CLK_MMIO_BYTES 0x00040000ul

/* A delay is reads of SRBM_STATUS, four a microsecond at least: an
 * uncached read over PCIe takes longer than 250 ns, and a longer wait is
 * the safe side of every DLL and PLL wait the tables make. */
#define V9X_SI_CLK_SRBM_STATUS      0x00000e50ul
#define V9X_SI_CLK_READS_PER_US     4ul

/* A clock within 0.1 MHz of its target is at it: the VBIOS's own readback
 * of 300 MHz was 29,999 (10 kHz units). */
#define V9X_SI_CLK_SLACK 10ul

/* The interpreter and its parameter space: static, past the HAL's
 * stack-frame limit, and serialised by the Win16 lock. */
static struct v9x_atom v9x_si_clk_atom;
static v9x_u32 v9x_si_clk_params[16];
static int v9x_si_clk_bad_offset;

static v9x_u32 v9x_si_clk_read(void *context, v9x_u32 offset)
{
    (void)context;
    if (offset >= V9X_SI_CLK_MMIO_BYTES) {
        v9x_si_clk_bad_offset = 1;
        return 0ul;
    }
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               offset);
}

static void v9x_si_clk_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    (void)context;
    if (offset >= V9X_SI_CLK_MMIO_BYTES) {
        v9x_si_clk_bad_offset = 1;
        return;
    }
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static void v9x_si_clk_delay(void *context, v9x_u32 microseconds)
{
    DWORD reads = microseconds * V9X_SI_CLK_READS_PER_US;

    (void)context;
    while (reads-- != 0ul) {
        (void)v9x_si_clk_read(0, V9X_SI_CLK_SRBM_STATUS);
    }
}

static const struct v9x_atom_io v9x_si_clk_io = {
    0, v9x_si_clk_read, v9x_si_clk_write, v9x_si_clk_delay
};

/* --- the record ---------------------------------------------------------- */

static void v9x_si_clk_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_si_clk_number(HANDLE file, const char *key, DWORD value)
{
    char digits[12];
    int count = 0;

    v9x_si_clk_text(file, key);
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < 11);
    while (count != 0) {
        char one[2];

        one[0] = digits[--count];
        one[1] = '\0';
        v9x_si_clk_text(file, one);
    }
    v9x_si_clk_text(file, "\r\n");
}

/* The outcome of one validation's look at the clocks. */
struct v9x_si_clk_report {
    const char *result;
    v9x_status plan_status;
    struct v9x_si_clock_plan plan;
    DWORD before_sclk;
    DWORD before_mclk;
    DWORD after_sclk;
    DWORD after_mclk;
    v9x_status sclk_status;
    v9x_status mclk_status;
};

static struct v9x_si_clk_report v9x_si_clk_last;

/* The last validation that set a clock, kept for as long as the HAL is
 * loaded, since a later revalidation rewrites the record. */
static const char *v9x_si_clk_last_set = "none";
static DWORD v9x_si_clk_last_set_sclk;
static DWORD v9x_si_clk_last_set_mclk;

static void v9x_si_clk_record(const struct v9x_si_clk_report *report)
{
    HANDLE file;

    file = CreateFileA(V9X_SI_CLK_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_si_clk_text(file, "[OlandClocks]\r\nResult=");
    v9x_si_clk_text(file, report->result);
    v9x_si_clk_text(file, "\r\n");
    v9x_si_clk_number(file, "Profile=", V9X_SI_CLOCKS_BOOT_VOLTAGE);
    v9x_si_clk_number(file, "PlanStatus=", report->plan_status);
    v9x_si_clk_number(file, "DefaultSclk10kHz=", report->plan.default_sclk);
    v9x_si_clk_number(file, "DefaultMclk10kHz=", report->plan.default_mclk);
    v9x_si_clk_number(file, "BootVddcMv=", report->plan.boot_vddc);
    v9x_si_clk_number(file, "Levels=", report->plan.levels);
    v9x_si_clk_number(file, "LevelsAtBootVddc=",
                      report->plan.levels_allowed);
    v9x_si_clk_number(file, "TargetSclk10kHz=", report->plan.sclk);
    v9x_si_clk_number(file, "TargetMclk10kHz=", report->plan.mclk);
    v9x_si_clk_number(file, "BeforeSclk10kHz=", report->before_sclk);
    v9x_si_clk_number(file, "BeforeMclk10kHz=", report->before_mclk);
    v9x_si_clk_number(file, "SetSclkStatus=", report->sclk_status);
    v9x_si_clk_number(file, "SetMclkStatus=", report->mclk_status);
    v9x_si_clk_number(file, "AfterSclk10kHz=", report->after_sclk);
    v9x_si_clk_number(file, "AfterMclk10kHz=", report->after_mclk);
    v9x_si_clk_number(file, "FaultOpcode=", v9x_si_clk_atom.fault_opcode);
    v9x_si_clk_number(file, "FaultOffset=", v9x_si_clk_atom.fault_offset);
    v9x_si_clk_text(file, "LastSet=");
    v9x_si_clk_text(file, v9x_si_clk_last_set);
    v9x_si_clk_text(file, "\r\n");
    v9x_si_clk_number(file, "LastSetSclk10kHz=", v9x_si_clk_last_set_sclk);
    v9x_si_clk_number(file, "LastSetMclk10kHz=", v9x_si_clk_last_set_mclk);
    CloseHandle(file);
}

/* --- the VBIOS ----------------------------------------------------------- */

/* The shadowed image, if it is readable, an option ROM, and the PCI data
 * structure names the device the engine is open on. 0 otherwise. */
static const v9x_u8 *v9x_si_clk_rom(DWORD device, DWORD *bytes)
{
    const v9x_u8 *rom = (const v9x_u8 *)V9X_SI_CLK_ROM_LINEAR;
    DWORD pcir;
    DWORD length;

    if (IsBadReadPtr(rom, V9X_SI_CLK_ROM_BLOCK) || rom[0] != 0x55u ||
        rom[1] != 0xaau) {
        return 0;
    }
    length = (DWORD)rom[2] * V9X_SI_CLK_ROM_BLOCK;
    if (length < V9X_SI_CLK_ROM_BLOCK || IsBadReadPtr(rom, length)) {
        return 0;
    }
    pcir = (DWORD)rom[V9X_SI_CLK_PCIR_POINTER] |
           ((DWORD)rom[V9X_SI_CLK_PCIR_POINTER + 1ul] << 8);
    if (pcir + 8ul > length ||
        *(const DWORD *)(rom + pcir) != V9X_SI_CLK_PCIR ||
        ((DWORD)rom[pcir + 4ul] | ((DWORD)rom[pcir + 5ul] << 8)) !=
            V9X_SI_CLK_VENDOR_ATI ||
        ((DWORD)rom[pcir + 6ul] | ((DWORD)rom[pcir + 7ul] << 8)) != device) {
        return 0;
    }
    *bytes = length;
    return rom;
}

/* A Get*Clock table's answer in 10 kHz units, 0 on failure. */
static DWORD v9x_si_clk_get(v9x_u32 table)
{
    DWORD index;

    for (index = 0ul; index < 16ul; ++index) {
        v9x_si_clk_params[index] = 0ul;
    }
    if (v9x_atom_execute(&v9x_si_clk_atom, table, v9x_si_clk_params, 16ul) !=
        V9X_STATUS_OK) {
        return 0ul;
    }
    return v9x_si_clk_params[0] & 0x00fffffful;
}

static v9x_status v9x_si_clk_set(v9x_u32 table, DWORD clock)
{
    DWORD index;

    for (index = 0ul; index < 16ul; ++index) {
        v9x_si_clk_params[index] = 0ul;
    }
    v9x_si_clk_params[0] = clock;
    return v9x_atom_execute(&v9x_si_clk_atom, table, v9x_si_clk_params, 16ul);
}

static int v9x_si_clk_at(DWORD clock, DWORD target)
{
    return clock + V9X_SI_CLK_SLACK >= target &&
           clock <= target + V9X_SI_CLK_SLACK;
}

int v9x_si_clocks_apply(DWORD device)
{
    struct v9x_si_clk_report *report = &v9x_si_clk_last;
    const v9x_u8 *rom;
    DWORD rom_bytes = 0ul;
    int changed = 0;

    report->result = "not-run";
    report->plan_status = V9X_STATUS_OK;
    report->plan.default_sclk = 0ul;
    report->plan.default_mclk = 0ul;
    report->plan.boot_vddc = 0ul;
    report->plan.levels = 0ul;
    report->plan.levels_allowed = 0ul;
    report->plan.sclk = 0ul;
    report->plan.mclk = 0ul;
    report->before_sclk = 0ul;
    report->before_mclk = 0ul;
    report->after_sclk = 0ul;
    report->after_mclk = 0ul;
    report->sclk_status = V9X_STATUS_OK;
    report->mclk_status = V9X_STATUS_OK;
    v9x_si_clk_bad_offset = 0;

    rom = v9x_si_clk_rom(device, &rom_bytes);
    if (rom == 0 || v9x_atom_init(&v9x_si_clk_atom, rom, rom_bytes,
                                  &v9x_si_clk_io) != V9X_STATUS_OK) {
        report->result = "no-vbios";
        v9x_si_clk_record(report);
        return 1;
    }
    report->plan_status = v9x_si_clocks_plan(&v9x_si_clk_atom,
                                             V9X_SI_CLOCKS_BOOT_VOLTAGE,
                                             &report->plan);
    report->before_sclk = v9x_si_clk_get(V9X_ATOM_CMD_GET_ENGINE_CLOCK);
    report->before_mclk = v9x_si_clk_get(V9X_ATOM_CMD_GET_MEMORY_CLOCK);
    if (report->plan_status != V9X_STATUS_OK || report->before_sclk == 0ul ||
        report->before_mclk == 0ul) {
        report->result = "not-understood";
        v9x_si_clk_record(report);
        return 1;
    }
    /* The Get tables only read: the clocks are reported under a running
     * CP too, and left as they are. */
    if (v9x_vi_cp_running()) {
        report->after_sclk = report->before_sclk;
        report->after_mclk = report->before_mclk;
        report->result = "cp-running";
        v9x_si_clk_record(report);
        return 1;
    }

    /* Engine first, then memory, as measured on the card. */
    if (!v9x_si_clk_at(report->before_sclk, report->plan.sclk)) {
        report->sclk_status = v9x_si_clk_set(V9X_ATOM_CMD_SET_ENGINE_CLOCK,
                                             report->plan.sclk);
        changed = 1;
    }
    if (report->sclk_status == V9X_STATUS_OK && !v9x_si_clk_bad_offset &&
        !v9x_si_clk_at(report->before_mclk, report->plan.mclk)) {
        report->mclk_status = v9x_si_clk_set(V9X_ATOM_CMD_SET_MEMORY_CLOCK,
                                             report->plan.mclk);
        changed = 1;
    }
    report->after_sclk = v9x_si_clk_get(V9X_ATOM_CMD_GET_ENGINE_CLOCK);
    report->after_mclk = v9x_si_clk_get(V9X_ATOM_CMD_GET_MEMORY_CLOCK);

    if (report->sclk_status != V9X_STATUS_OK ||
        report->mclk_status != V9X_STATUS_OK || v9x_si_clk_bad_offset) {
        /* A table stopped part way: the hardware may be half set. The
         * engine is not claimed on it. */
        report->result = "set-failed";
    } else if (!changed) {
        report->result = "already-set";
    } else if (v9x_si_clk_at(report->after_sclk, report->plan.sclk) &&
               v9x_si_clk_at(report->after_mclk, report->plan.mclk)) {
        report->result = "set";
    } else {
        report->result = "set-not-read-back";
    }
    if (changed) {
        v9x_si_clk_last_set = report->result;
        v9x_si_clk_last_set_sclk = report->after_sclk;
        v9x_si_clk_last_set_mclk = report->after_mclk;
    }
    v9x_si_clk_record(report);
    return report->sclk_status == V9X_STATUS_OK &&
           report->mclk_status == V9X_STATUS_OK && !v9x_si_clk_bad_offset;
}
