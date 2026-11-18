/*
 * An AtomBIOS command-table interpreter: runs an AMD VBIOS's own tables
 * (SetEngineClock, SetMemoryClock, ...) against the card through the
 * caller's register callbacks, as Linux's drivers/gpu/drm/radeon/atom.c
 * does.
 *
 * No Windows or DDK headers and no I/O of its own, so the host tests run it
 * on synthetic tables and fake registers. The semantics are those of
 * docs\probe\oland-clocks-2026-11-18\atom_sim.py, whose SetEngineClock and
 * SetMemoryClock traces, replayed on W9QI0DAD's Oland, changed both clocks;
 * its no-change runs ended with every register where it began
 * (docs\decisions\2026-11-18-oland-clocks-left-by-the-bios.md).
 *
 * Supported: register access in memory-mapped mode only (SET_ATI_PORT 0),
 * the parameter, workspace, data-table and scratch operands, every opcode
 * atom.c implements but REPEAT, SAVEREG and RESTOREREG. A table that asks
 * for anything else (indirect I/O, the PLL or MC operand) stops with
 * V9X_STATUS_UNSUPPORTED, before or after some of its writes: a caller
 * must treat any failure as "the hardware may be half way".
 *
 * Bounded: every ROM read is checked against the image, every parameter,
 * workspace and scratch index against its array, and a run stops with
 * V9X_STATUS_TIMEOUT after step_limit opcodes, as atom.c's watchdog stops a
 * poll that never completes.
 */
#ifndef VELOCITY9X_ATOMBIOS_H
#define VELOCITY9X_ATOMBIOS_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* The ROM header's offset at 48h, its "ATOM" at +4, and its master command
 * and data table lists at +1Eh and +20h (atombios.h ATOM_ROM_HEADER). */
#define V9X_ATOM_ROM_HEADER_POINTER  0x48ul
#define V9X_ATOM_ROM_SIGNATURE       0x4d4f5441ul /* "ATOM" */

/* Command tables (ATOM_MASTER_LIST_OF_COMMAND_TABLES). */
#define V9X_ATOM_CMD_SET_ENGINE_CLOCK 10u
#define V9X_ATOM_CMD_SET_MEMORY_CLOCK 11u
#define V9X_ATOM_CMD_GET_MEMORY_CLOCK 47u
#define V9X_ATOM_CMD_GET_ENGINE_CLOCK 48u

/* Data tables (ATOM_MASTER_LIST_OF_DATA_TABLES). */
#define V9X_ATOM_DATA_FIRMWARE_INFO   4u
#define V9X_ATOM_DATA_POWERPLAY_INFO  15u

/* Arrays the interpreter owns: workspace for every table on the call
 * chain, parameter space the caller passes in, and the FB scratch. */
#define V9X_ATOM_WS_DWORDS       256u
#define V9X_ATOM_SCRATCH_DWORDS  64u
#define V9X_ATOM_DEPTH_MAX       8u
#define V9X_ATOM_STEP_LIMIT      2000000ul

struct v9x_atom_io {
    void *context;
    /* Byte offsets into the register window. */
    v9x_u32 (*read)(void *context, v9x_u32 offset);
    void (*write)(void *context, v9x_u32 offset, v9x_u32 value);
    void (*delay_us)(void *context, v9x_u32 microseconds);
};

struct v9x_atom {
    const v9x_u8 *rom;
    v9x_u32 rom_bytes;
    v9x_u32 command_list;
    v9x_u32 data_list;
    const struct v9x_atom_io *io;
    /* Execution state, shared by every table on the call chain as atom.c
     * keeps it in its global context. */
    v9x_u32 divmul[2];
    v9x_u32 fb_base;
    v9x_u32 io_attr;
    v9x_u32 reg_block;
    v9x_u32 shift;
    v9x_u32 data_block;
    v9x_u32 io_mode;
    v9x_u32 steps;
    v9x_u32 step_limit;
    v9x_u32 depth;
    v9x_u32 ws_used;
    v9x_u32 ws[V9X_ATOM_WS_DWORDS];
    v9x_u32 scratch[V9X_ATOM_SCRATCH_DWORDS];
    /* The opcode and table offset a failure stopped at, for a report. */
    v9x_u32 fault_opcode;
    v9x_u32 fault_offset;
};

/* Checks the ROM's header and finds its two master lists. */
v9x_status v9x_atom_init(struct v9x_atom *atom, const v9x_u8 *rom,
                         v9x_u32 rom_bytes, const struct v9x_atom_io *io);

/* A data table's offset in the ROM, 0 when absent or out of the image. */
v9x_u32 v9x_atom_data_table(const struct v9x_atom *atom, v9x_u32 index);

/* Runs command table `index` with `params` as its parameter space
 * (param_dwords long, results written back into it). */
v9x_status v9x_atom_execute(struct v9x_atom *atom, v9x_u32 index,
                            v9x_u32 *params, v9x_u32 param_dwords);

/* Little-endian reads of the image, 0 past its end. */
v9x_u32 v9x_atom_u8(const struct v9x_atom *atom, v9x_u32 offset);
v9x_u32 v9x_atom_u16(const struct v9x_atom *atom, v9x_u32 offset);
v9x_u32 v9x_atom_u32(const struct v9x_atom *atom, v9x_u32 offset);

#endif
