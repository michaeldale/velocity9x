/*
 * The AtomBIOS interpreter (src\common\atombios.c) and the SI clock policy
 * (src\chipsets\ati\si\si_clocks.c).
 *
 * Synthetic tables exercise each operand form and opcode against a fake
 * register file. With V9X_OLAND_ROM naming W9QI0DAD's VBIOS dump (AMD's,
 * never in the tree) and V9X_OLAND_INI a V9XSIMM snapshot of that card, the
 * real SetEngineClock and SetMemoryClock run against the snapshot and must
 * give the results the probe replays measured on the card
 * (docs\decisions\2026-11-18-oland-clocks-left-by-the-bios.md); without
 * them those checks are skipped and say so.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velocity9x/atombios.h"
#include "velocity9x/si_clocks.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* --- a fake card ------------------------------------------------------- */

#define FAKE_REGS 0x10000ul   /* dwords: the first 256 KiB */

static v9x_u32 fake_reg[FAKE_REGS];
static unsigned char fake_known[FAKE_REGS];
static v9x_u32 fake_writes;
static v9x_u32 fake_delay_us;
/* A register that reads `ready_after` times before ready_bits appear. */
static v9x_u32 fake_poll_offset = 0xfffffffful;
static v9x_u32 fake_poll_reads;
static v9x_u32 fake_poll_after;
static v9x_u32 fake_poll_bits;
/* The two PLL handshakes the real tables poll: a change request answered
 * at once, as atom_sim.py modelled them. */
static int fake_pll_model;

static v9x_u32 fake_read(void *context, v9x_u32 offset)
{
    (void)context;
    if (offset / 4ul >= FAKE_REGS) {
        return 0ul;
    }
    if (offset == fake_poll_offset &&
        ++fake_poll_reads >= fake_poll_after) {
        fake_reg[offset / 4ul] |= fake_poll_bits;
    }
    return fake_reg[offset / 4ul];
}

static void fake_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    (void)context;
    if (offset / 4ul >= FAKE_REGS) {
        return;
    }
    fake_reg[offset / 4ul] = value;
    fake_known[offset / 4ul] = 1u;
    ++fake_writes;
    if (fake_pll_model) {
        if (offset == 0x604ul) {
            fake_reg[0x614ul / 4ul] |= 0x2ul;
        }
        if (offset == 0x2bb0ul && (value & (1ul << 14)) != 0ul) {
            fake_reg[offset / 4ul] |= 1ul << 16;
        }
    }
}

static void fake_delay(void *context, v9x_u32 microseconds)
{
    (void)context;
    fake_delay_us += microseconds;
}

static const struct v9x_atom_io fake_io = {
    0, fake_read, fake_write, fake_delay
};

static void fake_reset(void)
{
    memset(fake_reg, 0, sizeof(fake_reg));
    memset(fake_known, 0, sizeof(fake_known));
    fake_writes = 0ul;
    fake_delay_us = 0ul;
    fake_poll_offset = 0xfffffffful;
    fake_poll_reads = 0ul;
    fake_pll_model = 0;
}

/* --- a synthetic ROM ------------------------------------------------------- */

#define ROM_BYTES     0x2000u
#define ROM_HEADER    0x100u
#define ROM_COMMANDS  0x200u
#define ROM_DATA      0x300u
#define ROM_TABLES    0x400u

static v9x_u8 rom[ROM_BYTES];
static v9x_u32 rom_next;

static void put16(v9x_u32 at, v9x_u32 value)
{
    rom[at] = (v9x_u8)value;
    rom[at + 1u] = (v9x_u8)(value >> 8);
}

static void put32(v9x_u32 at, v9x_u32 value)
{
    put16(at, value & 0xfffful);
    put16(at + 2u, value >> 16);
}

/* An empty image: the option ROM signature, the ATOM header and two
 * master lists of 64 entries each. */
static void rom_reset(void)
{
    memset(rom, 0, sizeof(rom));
    rom[0] = 0x55u;
    rom[1] = 0xaau;
    put16(V9X_ATOM_ROM_HEADER_POINTER, ROM_HEADER);
    put32(ROM_HEADER + 4u, V9X_ATOM_ROM_SIGNATURE);
    put16(ROM_HEADER + 0x1eu, ROM_COMMANDS);
    put16(ROM_HEADER + 0x20u, ROM_DATA);
    put16(ROM_COMMANDS, 4u + 64u * 2u);
    put16(ROM_DATA, 4u + 64u * 2u);
    rom_next = ROM_TABLES;
}

/* Adds command table `index` with the given code, workspace dwords and
 * parameter bytes; returns its base. */
static v9x_u32 rom_table(v9x_u32 index, const v9x_u8 *code, v9x_u32 bytes,
                         v9x_u32 ws, v9x_u32 ps)
{
    v9x_u32 base = rom_next;

    put16(base, 6u + bytes);
    rom[base + 4u] = (v9x_u8)ws;
    rom[base + 5u] = (v9x_u8)ps;
    memcpy(&rom[base + 6u], code, bytes);
    put16(ROM_COMMANDS + 4u + index * 2u, base);
    rom_next = (base + 6u + bytes + 15u) & ~15u;
    return base;
}

static struct v9x_atom atom;

static v9x_status run(v9x_u32 index, v9x_u32 *params, v9x_u32 dwords)
{
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) == V9X_STATUS_OK);
    return v9x_atom_execute(&atom, index, params, dwords);
}

/* --- interpreter tests ------------------------------------------------------- */

static void test_header(void)
{
    rom_reset();
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) == V9X_STATUS_OK);
    rom[0] = 0u;
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) ==
          V9X_STATUS_UNSUPPORTED);
    rom_reset();
    put32(ROM_HEADER + 4u, 0x41544f4dul);
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) ==
          V9X_STATUS_UNSUPPORTED);
    rom_reset();
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* MOVE of a dword immediate into register 0x150 (byte 540h), and of a
 * byte immediate into the third byte of register 0x151, which keeps the
 * rest: attribute IMM, source BYTE0, destination selector 2. */
static void test_move(void)
{
    static const v9x_u8 code[] = {
        1u, 0x05u, 0x50u, 0x01u, 0x78u, 0x56u, 0x34u, 0x12u,
        1u, 0x05u | (4u << 3) | (2u << 6), 0x51u, 0x01u, 0xabu,
        91u
    };
    v9x_u32 params[4] = { 0ul, 0ul, 0ul, 0ul };

    fake_reset();
    rom_reset();
    rom_table(1u, code, sizeof(code), 0u, 0u);
    fake_reg[0x151u] = 0x11223344ul;
    CHECK(run(1u, params, 4ul) == V9X_STATUS_OK);
    CHECK(fake_reg[0x150u] == 0x12345678ul);
    CHECK(fake_reg[0x151u] == 0x11ab3344ul);
}

/* Parameters and workspace: ps[1] = ps[0] + 3 through workspace 0, the
 * remainder and quotient of ps[0] / 5, and a nested table that writes its
 * own ps[0], which is the caller's ps[ps_bytes / 4]. */
static void test_params_and_calls(void)
{
    static const v9x_u8 outer[] = {
        /* MOVE_WS ws0 <- ps0 (attr: src PS, DWORD) */
        3u, 0x01u, 0x00u, 0x00u,
        /* ADD_WS ws0 += imm 3 (attr: src IMM, DWORD) */
        45u, 0x05u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u,
        /* MOVE_PS ps1 <- ws0 (attr: src WS) */
        2u, 0x02u, 0x01u, 0x00u,
        /* DIV_PS ps0 / imm 5 */
        38u, 0x05u, 0x00u, 0x05u, 0x00u, 0x00u, 0x00u,
        /* MOVE_PS ps2 <- WS remainder (41h) */
        2u, 0x02u, 0x02u, 0x41u,
        /* MOVE_PS ps3 <- WS quotient (40h) */
        2u, 0x02u, 0x03u, 0x40u,
        /* CALL_TABLE 2 */
        82u, 0x02u,
        91u
    };
    static const v9x_u8 inner[] = {
        /* MOVE_PS ps0 <- imm 99 */
        2u, 0x05u, 0x00u, 99u, 0x00u, 0x00u, 0x00u,
        91u
    };
    v9x_u32 params[8] = { 17ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul };

    fake_reset();
    rom_reset();
    rom_table(1u, outer, sizeof(outer), 2u, 16u);
    rom_table(2u, inner, sizeof(inner), 0u, 4u);
    CHECK(run(1u, params, 8ul) == V9X_STATUS_OK);
    CHECK(params[1] == 20ul);
    CHECK(params[2] == 2ul);
    CHECK(params[3] == 3ul);
    CHECK(params[4] == 99ul);
    CHECK(params[0] == 17ul);
}

/* A poll: TEST register 0x185 (614h) against bit 1, JUMP_EQUAL back while
 * it is clear; the fake sets it on the fifth read. Then the same table
 * with a bit that never comes, which the step limit ends. */
static void test_poll_and_limit(void)
{
    v9x_u8 code[16];
    v9x_u32 params[2] = { 0ul, 0ul };
    v9x_u32 base;

    /* TEST_REG reg 0185h & imm 2; JUMP_EQUAL to the TEST; EOT. The jump
     * target is relative to the table's base: the code starts at 6. */
    code[0] = 74u; code[1] = 0x05u; code[2] = 0x85u; code[3] = 0x01u;
    code[4] = 0x02u; code[5] = 0x00u; code[6] = 0x00u; code[7] = 0x00u;
    code[8] = 68u; code[9] = 6u; code[10] = 0u;
    code[11] = 91u;

    fake_reset();
    rom_reset();
    base = rom_table(1u, code, 12u, 0u, 0u);
    (void)base;
    fake_poll_offset = 0x614ul;
    fake_poll_after = 5ul;
    fake_poll_bits = 0x2ul;
    CHECK(run(1u, params, 2ul) == V9X_STATUS_OK);
    CHECK(fake_poll_reads == 5ul);

    fake_reset();
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) == V9X_STATUS_OK);
    atom.step_limit = 1000ul;
    CHECK(v9x_atom_execute(&atom, 1u, params, 2ul) == V9X_STATUS_TIMEOUT);
}

/* SWITCH on a parameter, MASK, SHL and CLEAR. */
static void test_switch_mask_shift(void)
{
    v9x_u32 params[4];

    {
        /* SWITCH on ps0's low byte: 7 -> 26, 9 -> 33 (EOT), anything else
         * falls through both MOVEs. Targets are offsets from the table's
         * base; its code starts at 6. */
        static const v9x_u8 sw[] = {
            66u, 0x01u | (4u << 3), 0x00u,   /* 6..8 */
            0x63u, 7u, 26u, 0u,               /* 9..12 */
            0x63u, 9u, 33u, 0u,               /* 13..16 */
            0x5au, 0x5au,                     /* 17..18 */
            2u, 0x05u, 0x01u, 1u, 0u, 0u, 0u, /* 19..25: ps1 <- 1 */
            2u, 0x05u, 0x01u, 2u, 0u, 0u, 0u, /* 26..32: ps1 <- 2 */
            91u,                              /* 33 */
        };
        fake_reset();
        rom_reset();
        rom_table(1u, sw, sizeof(sw), 0u, 0u);
        params[0] = 7ul; params[1] = 0ul;
        CHECK(run(1u, params, 4ul) == V9X_STATUS_OK);
        CHECK(params[1] == 2ul);
        params[0] = 9ul; params[1] = 0ul;
        CHECK(run(1u, params, 4ul) == V9X_STATUS_OK);
        CHECK(params[1] == 0ul);
        params[0] = 3ul; params[1] = 0ul;
        CHECK(run(1u, params, 4ul) == V9X_STATUS_OK);
        CHECK(params[1] == 2ul);
    }
    {
        static const v9x_u8 ops[] = {
            /* MASK_REG reg 0150h: (reg & FF00FFFF) | imm 00120000 */
            92u, 0x05u, 0x50u, 0x01u,
            0xffu, 0xffu, 0x00u, 0xffu, 0x00u, 0x00u, 0x12u, 0x00u,
            /* SHL_PS ps0 <<= imm 4 (attr src IMM, DWORD) */
            110u, 0x05u, 0x00u, 4u, 0u, 0u, 0u,
            /* CLEAR_PS ps1 (attr: DWORD) */
            85u, 0x00u, 0x01u,
            91u
        };
        fake_reset();
        rom_reset();
        rom_table(1u, ops, sizeof(ops), 0u, 0u);
        fake_reg[0x150u] = 0xaabbccddul;
        params[0] = 0x0123ul; params[1] = 77ul;
        CHECK(run(1u, params, 4ul) == V9X_STATUS_OK);
        CHECK(fake_reg[0x150u] == 0xaa12ccddul);
        CHECK(params[0] == 0x1230ul);
        CHECK(params[1] == 0ul);
    }
}

/* What the interpreter refuses: indirect I/O, REPEAT, a parameter past the
 * caller's array, a missing table, and DELAY's microseconds counted. */
static void test_refusals(void)
{
    static const v9x_u8 iio[] = {
        55u, 0x01u, 0x00u,                  /* SET_ATI_PORT 1 */
        1u, 0x05u, 0x50u, 0x01u, 1u, 0u, 0u, 0u,
        91u
    };
    static const v9x_u8 repeat[] = { 83u, 91u };
    static const v9x_u8 far_ps[] = {
        2u, 0x05u, 0x09u, 1u, 0u, 0u, 0u, 91u
    };
    static const v9x_u8 delays[] = { 81u, 15u, 80u, 2u, 91u };
    v9x_u32 params[4] = { 0ul, 0ul, 0ul, 0ul };

    fake_reset();
    rom_reset();
    rom_table(1u, iio, sizeof(iio), 0u, 0u);
    rom_table(2u, repeat, sizeof(repeat), 0u, 0u);
    rom_table(3u, far_ps, sizeof(far_ps), 0u, 0u);
    rom_table(4u, delays, sizeof(delays), 0u, 0u);
    CHECK(run(1u, params, 4ul) == V9X_STATUS_UNSUPPORTED);
    CHECK(fake_writes == 0ul);
    CHECK(run(2u, params, 4ul) == V9X_STATUS_UNSUPPORTED);
    CHECK(run(3u, params, 4ul) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(run(9u, params, 4ul) == V9X_STATUS_UNSUPPORTED);
    CHECK(run(4u, params, 4ul) == V9X_STATUS_OK);
    CHECK(fake_delay_us == 2015ul);
}

/* --- the clock policy on a synthetic VBIOS -------------------------------- */

#define FW_AT  0x1000u
#define PP_AT  0x1100u

static void clock_entry(v9x_u32 at, v9x_u32 sclk, v9x_u32 mclk, v9x_u32 vddc)
{
    put16(at, sclk & 0xfffful);
    rom[at + 2u] = (v9x_u8)(sclk >> 16);
    put16(at + 3u, mclk & 0xfffful);
    rom[at + 5u] = (v9x_u8)(mclk >> 16);
    put16(at + 6u, vddc);
}

/* W9QI0DAD's tables in miniature: boot 300/299 MHz at 900 mV; clock
 * entries 0 1050/1000 at 1200, 1 500/1125 at 800 (named by no state),
 * 2 300/300 at 800, 3 400/1000 at 900; states boot [0], performance
 * [2, 3, 0]. */
static void build_clock_rom(void)
{
    v9x_u32 clocks = PP_AT + 0x20u;
    v9x_u32 states = PP_AT + 0x60u;

    rom_reset();
    put16(ROM_DATA + 4u + V9X_ATOM_DATA_FIRMWARE_INFO * 2u, FW_AT);
    put16(FW_AT, 108u);
    rom[FW_AT + 2u] = 2u;
    rom[FW_AT + 3u] = 2u;
    put32(FW_AT + V9X_SI_FW_DEFAULT_SCLK, 30000ul);
    put32(FW_AT + V9X_SI_FW_DEFAULT_MCLK, 29900ul);
    put16(FW_AT + V9X_SI_FW_BOOT_VDDC, 900u);

    put16(ROM_DATA + 4u + V9X_ATOM_DATA_POWERPLAY_INFO * 2u, PP_AT);
    put16(PP_AT, 0xa0u);
    rom[PP_AT + 2u] = 6u;
    rom[PP_AT + 3u] = 1u;
    put16(PP_AT + V9X_SI_PP_STATE_ARRAY, states - PP_AT);
    put16(PP_AT + V9X_SI_PP_CLOCK_ARRAY, clocks - PP_AT);
    rom[clocks] = 4u;
    rom[clocks + 1u] = 16u;
    clock_entry(clocks + 2u, 105000ul, 100000ul, 1200u);
    clock_entry(clocks + 18u, 50000ul, 112500ul, 800u);
    clock_entry(clocks + 34u, 30000ul, 30000ul, 800u);
    clock_entry(clocks + 50u, 40000ul, 100000ul, 900u);
    rom[states] = 2u;
    rom[states + 1u] = 1u; rom[states + 2u] = 0u; rom[states + 3u] = 0u;
    rom[states + 4u] = 3u; rom[states + 5u] = 1u;
    rom[states + 6u] = 2u; rom[states + 7u] = 3u; rom[states + 8u] = 0u;
}

static void test_clock_policy(void)
{
    struct v9x_si_clock_plan plan;

    build_clock_rom();
    CHECK(v9x_atom_init(&atom, rom, ROM_BYTES, &fake_io) == V9X_STATUS_OK);
    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BIOS, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.sclk == 30000ul && plan.mclk == 29900ul);
    CHECK(plan.boot_vddc == 900ul);

    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BOOT_VOLTAGE, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.levels == 4ul);
    CHECK(plan.levels_allowed == 2ul);
    CHECK(plan.sclk == 40000ul);
    CHECK(plan.mclk == 100000ul);

    CHECK(v9x_si_clocks_plan(&atom, 7ul, &plan) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* A boot VDDC below every level leaves the defaults. */
    put16(FW_AT + V9X_SI_FW_BOOT_VDDC, 700u);
    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BOOT_VOLTAGE, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.sclk == 30000ul && plan.mclk == 29900ul);
    CHECK(plan.levels_allowed == 0ul);

    /* Another PowerPlay revision is not read. */
    put16(FW_AT + V9X_SI_FW_BOOT_VDDC, 900u);
    rom[PP_AT + 2u] = 7u;
    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BOOT_VOLTAGE, &plan) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(plan.sclk == 30000ul);

    /* A state naming a clock entry past the array is not read. */
    rom[PP_AT + 2u] = 6u;
    rom[PP_AT + 0x60u + 6u] = 9u;
    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BOOT_VOLTAGE, &plan) ==
          V9X_STATUS_UNSUPPORTED);
}

/* --- W9QI0DAD's own VBIOS, when it is at hand ------------------------------ */

static v9x_u8 real_rom[0x20000];
static v9x_u32 real_rom_bytes;

static int load_real(void)
{
    const char *rom_path = getenv("V9X_OLAND_ROM");
    const char *ini_path = getenv("V9X_OLAND_INI");
    FILE *file;
    char line[128];
    unsigned long offset;
    unsigned long value;

    if (rom_path == 0 || ini_path == 0) {
        return 0;
    }
    file = fopen(rom_path, "rb");
    if (file == 0) {
        return 0;
    }
    real_rom_bytes = (v9x_u32)fread(real_rom, 1u, sizeof(real_rom), file);
    fclose(file);
    file = fopen(ini_path, "r");
    if (file == 0) {
        return 0;
    }
    fake_reset();
    while (fgets(line, (int)sizeof(line), file) != 0) {
        if (sscanf(line, "R%5lx=%8lx", &offset, &value) == 2 &&
            offset / 4ul < FAKE_REGS) {
            fake_reg[offset / 4ul] = (v9x_u32)value;
        }
    }
    fclose(file);
    return 1;
}

/* GetEngineClock against the snapshot; SetEngineClock 400 MHz's FB_DIV;
 * SetMemoryClock 1000 MHz's MPLL; and the policy's targets. The snapshot
 * V9XSIMM-3.INI was taken with the engine already at 500 MHz. */
static void test_real_vbios(void)
{
    struct v9x_si_clock_plan plan;
    v9x_u32 params[16];
    v9x_status status;

    if (!load_real()) {
        printf("SKIP atombios: V9X_OLAND_ROM / V9X_OLAND_INI not set\n");
        return;
    }
    fake_pll_model = 1;
    CHECK(v9x_atom_init(&atom, real_rom, real_rom_bytes, &fake_io) ==
          V9X_STATUS_OK);

    memset(params, 0, sizeof(params));
    CHECK(v9x_atom_execute(&atom, V9X_ATOM_CMD_GET_ENGINE_CLOCK, params,
                           16ul) == V9X_STATUS_OK);
    /* The VBIOS's own rounding: 29,999 at 300 MHz on the card. */
    CHECK(params[0] >= 49990ul && params[0] <= 50000ul);

    memset(params, 0, sizeof(params));
    params[0] = 40000ul;
    status = v9x_atom_execute(&atom, V9X_ATOM_CMD_SET_ENGINE_CLOCK, params,
                              16ul);
    CHECK(status == V9X_STATUS_OK);
    CHECK(fake_reg[0x608u / 4u] == 0x100ed090ul);
    CHECK(fake_reg[0x600u / 4u] == 0x80400000ul);
    CHECK(fake_reg[0x27b0u / 4u] == 0x17171717ul);

    memset(params, 0, sizeof(params));
    params[0] = 100000ul;
    status = v9x_atom_execute(&atom, V9X_ATOM_CMD_SET_MEMORY_CLOCK, params,
                              16ul);
    CHECK(status == V9X_STATUS_OK);
    CHECK(fake_reg[0x2bb8u / 4u] == 0x004a0961ul);
    CHECK(fake_reg[0x2bc0u / 4u] == 0ul);
    CHECK(fake_reg[0x5490u / 4u] == 3ul);
    CHECK(fake_reg[0x20acu / 4u] == 0ul);
    CHECK(fake_reg[0x28a0u / 4u] == 0x336739ceul);
    CHECK(fake_reg[0x2aacu / 4u] == 0x00001124ul);

    memset(params, 0, sizeof(params));
    CHECK(v9x_atom_execute(&atom, V9X_ATOM_CMD_GET_MEMORY_CLOCK, params,
                           16ul) == V9X_STATUS_OK);
    printf("atombios: real VBIOS GetMemoryClock after 1000 MHz: %lu\n",
           (unsigned long)params[0]);

    CHECK(v9x_si_clocks_plan(&atom, V9X_SI_CLOCKS_BOOT_VOLTAGE, &plan) ==
          V9X_STATUS_OK);
    CHECK(plan.default_sclk == 30000ul && plan.default_mclk == 29900ul);
    CHECK(plan.boot_vddc == 900ul);
    CHECK(plan.sclk == 40000ul);
    CHECK(plan.mclk == 100000ul);
}

unsigned int v9x_run_atombios_tests(void)
{
    failures = 0u;
    test_header();
    test_move();
    test_params_and_calls();
    test_poll_and_limit();
    test_switch_mask_shift();
    test_refusals();
    test_clock_policy();
    test_real_vbios();
    return failures;
}
