/*
 * A capture of the DrawPrimitives2 calls, a documented diagnostic
 * (docs\plans\ddi6-drawprimitives2.md, Part C).
 *
 * Half-Life arrives at DDI 6 as one DrawPrimitives2 call for about every
 * 1.3 of its draws where the DX5 path took about 4.6 draws a call, and the
 * driver cannot see why from counters: what ends a call is decided in the
 * runtime. This records, per call, what the call carried (every record,
 * every render state with its value), the buffers and offsets the runtime
 * used, the time spent inside the call and the time since the previous
 * one, and interleaved with the calls every other HAL entry point the
 * runtime reached between them. What precedes the start of each call is
 * then the runtime's reason for having ended the previous one.
 *
 * Armed by [Velocity9x] Dp2Capture=1, for a process that gets DDI 6. The
 * first Dp2RingSkip DrawPrimitives2 calls (same section), and every call
 * in the first Dp2RingDelayMs after the first one, are let through
 * unrecorded; then V9X_DP2_RING_ENTRIES entries are kept and written once to
 * C:\V9XDIAG\V9XDP2R.BIN, when full or when the process's last context is
 * destroyed, whichever is first. tools\diag\dp2ring.py reads it.
 */
#ifndef VELOCITY9X_D3D_DP2_RING_H
#define VELOCITY9X_D3D_DP2_RING_H

#include "velocity9x/types.h"

/* Entry kinds. 1 is a DrawPrimitives2 call; the rest are HAL entry points
 * reached between calls, with their two arguments as noted. */
#define V9X_DP2R_CALL            1ul
#define V9X_DP2R_LOCK            2ul  /* surface, flags                     */
#define V9X_DP2R_UNLOCK          3ul  /* surface                            */
#define V9X_DP2R_BLT             4ul  /* destination, flags                 */
#define V9X_DP2R_FLIP            5ul  /* target, flags                      */
#define V9X_DP2R_CREATE_SURFACE  6ul  /* first surface, count               */
#define V9X_DP2R_DESTROY_SURFACE 7ul  /* surface                            */
#define V9X_DP2R_TEXTURE_CREATE  8ul  /* surface, handle given              */
#define V9X_DP2R_TEXTURE_DESTROY 9ul  /* handle                             */
#define V9X_DP2R_TEXTURE_SWAP   10ul  /* handle 1, handle 2                 */
#define V9X_DP2R_TEXTURE_GETSURF 11ul /* handle                             */
#define V9X_DP2R_CLEAR2         12ul  /* flags, rectangles                  */
#define V9X_DP2R_VALIDATE_TSS   13ul  /* context                            */
#define V9X_DP2R_RENDER_STATE   14ul  /* DX3 execute RenderState            */
#define V9X_DP2R_SET_TARGET     15ul  /* surface                            */
#define V9X_DP2R_CONTEXT_CREATE 16ul  /* context                            */
#define V9X_DP2R_CONTEXT_DESTROY 17ul /* context                            */
#define V9X_DP2R_DX5_DRAW       18ul  /* any DX5 draw entry point           */
#define V9X_DP2R_GET_DRIVER_INFO 19ul /* GUID Data1                         */
#define V9X_DP2R_WAIT_FLIP      20ul  /* GetFlipStatus / GetBltStatus       */
#define V9X_DP2R_SCAN_LINE      21ul  /* GetScanLine / WaitForVerticalBlank */

/* What each call carried, one item per record, and one per render state
 * or texture stage state inside a state record, until the slots run out
 * (items keeps counting). */
typedef struct v9x_dp2r_item {
    v9x_u8 op;                   /* D3DHAL_DP2OPERATION                */
    v9x_u8 count;                /* the record's count, clamped to 255 */
    v9x_u16 state;               /* render state, or stage << 8 | TSS  */
    v9x_u32 value;               /* its value; else the payload's first
                                    DWORD, or 0                        */
} V9X_DP2R_ITEM;

#define V9X_DP2R_ITEMS 24u

typedef struct v9x_dp2r_entry {
    v9x_u32 kind;
    v9x_u32 tsc_in;              /* low TSC at entry                   */
    v9x_u32 tsc_out;             /* low TSC at exit (calls only)       */
    v9x_u32 flags;               /* dwFlags, or the event's first arg  */
    v9x_u32 command_offset;      /* or the event's second arg          */
    v9x_u32 command_length;
    v9x_u32 vertex_offset;
    v9x_u32 vertex_length;
    v9x_u32 command_surface;
    v9x_u32 vertex_surface;      /* or the user-memory pointer         */
    v9x_u32 vertex_type;
    v9x_u32 records;
    v9x_u32 triangles;
    v9x_u32 req_vertex;          /* dwReqVertexBufSize on the way in   */
    v9x_u32 req_command;         /* dwReqCommandBufSize                */
    v9x_u32 items;               /* items seen, may exceed the slots   */
    V9X_DP2R_ITEM item[V9X_DP2R_ITEMS];
} V9X_DP2R_ENTRY;

#define V9X_DP2_RING_ENTRIES 8192ul

/* Non-zero while entries are being kept; read inline by the event macro so
 * an unarmed driver pays one compare. */
extern volatile v9x_u32 v9x_dp2_ring_live;

/* At the start of each DrawPrimitives2 call: arms on the first call with
 * the probe bit, counts the skip, and returns the entry to fill or null. */
V9X_DP2R_ENTRY *v9x_dp2_ring_call(v9x_u32 probe_armed);
/* A record of the call in progress (a V9X_DP2_SINK record callback). */
void v9x_dp2_ring_record(V9X_DP2R_ENTRY *entry, v9x_u32 op, v9x_u32 count,
                         const v9x_u8 *payload, v9x_u32 bytes);
/* The call is complete: stamps its exit time, writes the file when full. */
void v9x_dp2_ring_done(V9X_DP2R_ENTRY *entry);
void v9x_dp2_ring_event_slow(v9x_u32 kind, v9x_u32 a, v9x_u32 b);
/* Write what there is, if anything, and stop. */
void v9x_dp2_ring_flush(void);
/* At context creation: forget any earlier capture and arm afresh. */
void v9x_dp2_ring_rearm(void);

#define V9X_DP2_RING_EVENT(kind, a, b)                                   \
    do {                                                                 \
        if (v9x_dp2_ring_live != 0ul) {                                  \
            v9x_dp2_ring_event_slow((kind), (v9x_u32)(a), (v9x_u32)(b)); \
        }                                                                \
    } while (0)

#endif
