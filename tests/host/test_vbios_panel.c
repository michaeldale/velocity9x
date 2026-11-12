/*
 * The panel-mode slot patch (src\common\vbios_panel.c), over synthetic ROMs
 * laid out as TOMMOHP's GM965 BIOS is: a mode table at 268h whose entries
 * 60h-65h point at two 26-byte slots headed 1280x768 and 1280x800, both
 * holding a 1024x768 descriptor. The real image is kept outside the repo;
 * these bytes reproduce its structure, not its content.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/vbios_panel.h"

static unsigned int vbios_failures = 0u;

#define VBIOSCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #expression); \
        ++vbios_failures; \
    } \
} while (0)

#define ROM_BYTES 0xEE00u          /* 119 blocks of 512, as TOMMOHP's */
#define TABLE_AT 0x0268u
#define STANDARD_BLOCK 0x0136u
#define SLOT_768 0x129Au
#define SLOT_800 0x12B4u

/* TOMMOHP's placeholder: 1024x768 at 65 MHz, as its slots hold it. */
static const v9x_u8 placeholder[18] = {
    0x64u, 0x19u, 0x00u, 0x40u, 0x41u, 0x00u, 0x26u, 0x30u, 0x18u,
    0x88u, 0x36u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x18u
};

static v9x_u8 rom[ROM_BYTES];

static void put_word(v9x_u8 *at, v9x_u16 value)
{
    at[0] = (v9x_u8)(value & 0xffu);
    at[1] = (v9x_u8)(value >> 8);
}

static void put_entry(v9x_u16 at, v9x_u8 mode, v9x_u8 bpp, v9x_u16 block)
{
    rom[at] = mode;
    rom[at + 1u] = bpp;
    put_word(&rom[at + 2u], block);
    rom[at + 4u] = 0u;
}

static void put_slot(v9x_u16 block, v9x_u16 width, v9x_u16 height)
{
    rom[block] = 0x03u;
    rom[block + 1u] = 0xffu;
    put_word(&rom[block + 2u], width);
    put_word(&rom[block + 4u], height);
    rom[block + 6u] = 0x07u;
    rom[block + 7u] = 0x3cu;
    memcpy(&rom[block + 8u], placeholder, sizeof(placeholder));
}

/* The ROM: three standard entries, then 60h-65h on the two slots given. */
static void make_rom(v9x_u16 slot_768, v9x_u16 slot_800)
{
    v9x_u16 at = TABLE_AT;

    memset(rom, 0, sizeof(rom));
    rom[0] = 0x55u;
    rom[1] = 0xaau;
    rom[2] = (v9x_u8)(ROM_BYTES / 512u);
    put_entry(at, 0x30u, 8u, STANDARD_BLOCK); at += 5u;
    put_entry(at, 0x32u, 8u, STANDARD_BLOCK); at += 5u;
    put_entry(at, 0x34u, 8u, STANDARD_BLOCK); at += 5u;
    put_entry(at, 0x60u, 8u, slot_768); at += 5u;
    put_entry(at, 0x61u, 16u, slot_768); at += 5u;
    put_entry(at, 0x62u, 32u, slot_768); at += 5u;
    put_entry(at, 0x63u, 8u, slot_800); at += 5u;
    put_entry(at, 0x64u, 16u, slot_800); at += 5u;
    put_entry(at, 0x65u, 32u, slot_800); at += 5u;
    rom[at] = 0xffu;
    put_slot(slot_768, 1280u, 768u);
    put_slot(slot_800, 1280u, 800u);
}

/* An EDID 1.4 block whose preferred timing is the panel's: 71.11 MHz,
 * 1280 + 160, 800 + 23, as TOMMOHP's VBT DTD and pipe B read. */
static void make_edid(v9x_u8 *block, v9x_u16 width, v9x_u16 height)
{
    static const v9x_u8 header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u
    };
    v9x_u8 *dtd = block + 54;
    unsigned int index;
    unsigned int sum = 0u;

    memset(block, 0, 128);
    memcpy(block, header, 8);
    block[18] = 1u;
    block[19] = 4u;
    put_word(dtd, 7111u);
    dtd[2] = (v9x_u8)(width & 0xffu);
    dtd[3] = 160u;
    dtd[4] = (v9x_u8)((width >> 4) & 0xf0u);
    dtd[5] = (v9x_u8)(height & 0xffu);
    dtd[6] = 23u;
    dtd[7] = (v9x_u8)((height >> 4) & 0xf0u);
    dtd[8] = 48u;
    dtd[9] = 32u;
    dtd[10] = 0x36u;
    dtd[17] = 0x18u;
    for (index = 0u; index < 127u; ++index) {
        sum += block[index];
    }
    block[127] = (v9x_u8)(0x100u - (sum & 0xffu));
}

static void test_vbios_plans_the_panel_slot(void)
{
    v9x_u8 edid[128];
    struct v9x_vbios_patch patch;

    make_rom(SLOT_768, SLOT_800);
    make_edid(edid, 1280u, 800u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_PLANNED);
    VBIOSCHECK(patch.rom_offset == SLOT_800 + 8u);
    VBIOSCHECK(patch.pam_register == 0x91u);
    VBIOSCHECK(patch.pam_rw == 0x03u);
    VBIOSCHECK(patch.width == 1280u && patch.height == 800u);
    VBIOSCHECK(memcmp(patch.old_bytes, placeholder, 18) == 0);
    VBIOSCHECK(memcmp(patch.new_bytes, edid + 54, 18) == 0);

    /* Once the slot holds the panel's descriptor, there is nothing to do. */
    memcpy(&rom[SLOT_800 + 8u], edid + 54, 18);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_ALREADY);
    VBIOSCHECK(patch.rom_offset == SLOT_800 + 8u);

    /* The 1280x768 slot is the other one, never touched for an 800 panel. */
    make_rom(SLOT_768, SLOT_800);
    make_edid(edid, 1280u, 768u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_PLANNED);
    VBIOSCHECK(patch.rom_offset == SLOT_768 + 8u);
}

static void test_vbios_picks_the_pam_half(void)
{
    v9x_u8 edid[128];
    struct v9x_vbios_patch patch;

    make_edid(edid, 1280u, 800u);

    /* C4000h-C7FFFh: PAM1's high half. */
    make_rom(SLOT_768, 0x4100u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_PLANNED);
    VBIOSCHECK(patch.pam_register == 0x91u && patch.pam_rw == 0x30u);

    /* C8000h on: PAM2. */
    make_rom(SLOT_768, 0x8100u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_PLANNED);
    VBIOSCHECK(patch.pam_register == 0x92u && patch.pam_rw == 0x03u);
    make_rom(SLOT_768, 0xC100u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_PLANNED);
    VBIOSCHECK(patch.pam_register == 0x92u && patch.pam_rw == 0x30u);

    /* A descriptor across 4000h would need two halves open: refused. */
    make_rom(SLOT_768, 0x3FF0u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_SPLIT);
}

static void test_vbios_refuses_what_it_cannot_recognise(void)
{
    v9x_u8 edid[128];
    struct v9x_vbios_patch patch;

    make_edid(edid, 1280u, 800u);

    /* No usable EDID. */
    make_rom(SLOT_768, SLOT_800);
    edid[127] ^= 1u;
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NO_EDID);
    VBIOSCHECK(patch.rom_offset == 0u);
    make_edid(edid, 1280u, 800u);

    /* Not a ROM. */
    rom[0] = 0u;
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NOT_ROM);

    /* No table, and two tables. */
    make_rom(SLOT_768, SLOT_800);
    rom[TABLE_AT + 5u] = 0x33u;
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NO_TABLE);
    make_rom(SLOT_768, SLOT_800);
    rom[0x2000u] = 0x30u;
    rom[0x2005u] = 0x32u;
    rom[0x200Au] = 0x34u;
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_TABLES);

    /* A table past the ROM's own length is not looked for. */
    make_rom(SLOT_768, SLOT_800);
    rom[2] = 1u;
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NO_TABLE);

    /* A panel size no slot is headed with. */
    make_rom(SLOT_768, SLOT_800);
    make_edid(edid, 1366u, 768u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NO_SLOT);

    /* Two distinct blocks headed the panel's size: ambiguous. */
    make_rom(SLOT_768, SLOT_800);
    make_edid(edid, 1280u, 800u);
    put_slot(SLOT_768, 1280u, 800u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_SLOTS);

    /* Below 60h are the standard modes, whatever their blocks say. */
    make_rom(SLOT_768, SLOT_800);
    put_slot(STANDARD_BLOCK, 1280u, 800u);
    put_slot(SLOT_800, 0u, 0u);
    VBIOSCHECK(v9x_vbios_panel_plan(rom, ROM_BYTES, edid, &patch) ==
               V9X_VBIOS_PANEL_NO_SLOT);
}

static void test_vbios_formats_the_record(void)
{
    struct v9x_vbios_patch patch;
    char text[V9X_VBIOS_PATCH_TEXT_BYTES];
    unsigned int index;

    memset(&patch, 0, sizeof(patch));
    patch.rom_offset = 0x12BCu;
    patch.pam_register = 0x91u;
    patch.pam_rw = 0x03u;
    for (index = 0u; index < 18u; ++index) {
        patch.old_bytes[index] = (v9x_u8)index;
        patch.new_bytes[index] = (v9x_u8)(0xF0u + index);
    }
    v9x_vbios_panel_format(&patch, text);
    VBIOSCHECK(strlen(text) == V9X_VBIOS_PATCH_TEXT_BYTES - 1u);
    VBIOSCHECK(strncmp(text, "12BC 91 03 000102", 17) == 0);
    /* The old descriptor ends at 46, the new one starts at 48. */
    VBIOSCHECK(strncmp(text + 45, "11 F0F1", 7) == 0);
    VBIOSCHECK(strcmp(text + 80, "0001") == 0);
}

unsigned int v9x_run_vbios_panel_tests(void)
{
    test_vbios_plans_the_panel_slot();
    test_vbios_picks_the_pam_half();
    test_vbios_refuses_what_it_cannot_recognise();
    test_vbios_formats_the_record();
    return vbios_failures;
}
