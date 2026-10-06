/*
 * The DrawPrimitives2 capture; d3d_dp2_ring.h says what it is for. An
 * instrument: nothing here runs unless Direct3DDdiProbe bit 32 is set.
 */
#include "d3d_internal.h"
#include "d3d_dp2_ring.h"

#define V9X_DP2_RING_PATH "C:\\V9XDIAG\\V9XDP2R.BIN"

/* File header; the entries follow it. */
typedef struct v9x_dp2r_header {
    v9x_u32 magic;               /* 'V9R1'                              */
    v9x_u32 entry_bytes;
    v9x_u32 entries;
    v9x_u32 skipped_calls;
    v9x_u32 dropped_events;      /* events before the first kept call   */
} V9X_DP2R_HEADER;

#define V9X_DP2R_MAGIC 0x31523956ul

/* 0 never armed, 1 skipping, 2 keeping, 3 written. NOT per process: on
 * Windows 98 this DLL's data is shared by every DirectDraw process (a
 * second Half-Life run found it still at 3), while the VirtualAlloc buffer
 * belongs to the process that made it. v9x_dp2_ring_rearm starts over at
 * each context creation. */
static v9x_u32 v9x_dp2_ring_phase;
volatile v9x_u32 v9x_dp2_ring_live;
static V9X_DP2R_ENTRY *v9x_dp2_ring;
static v9x_u32 v9x_dp2_ring_used;
static v9x_u32 v9x_dp2_ring_skip;
static v9x_u32 v9x_dp2_ring_skipped;
static v9x_u32 v9x_dp2_ring_delay;
static v9x_u32 v9x_dp2_ring_armed_tick;
static v9x_u32 v9x_dp2_ring_pid;

static void v9x_dp2_ring_write(void)
{
    V9X_DP2R_HEADER header;
    HANDLE file;
    DWORD written;

    v9x_dp2_ring_live = 0ul;
    v9x_dp2_ring_phase = 3ul;
    if (v9x_dp2_ring == 0) {
        return;
    }
    header.magic = V9X_DP2R_MAGIC;
    header.entry_bytes = (v9x_u32)sizeof(V9X_DP2R_ENTRY);
    header.entries = v9x_dp2_ring_used;
    header.skipped_calls = v9x_dp2_ring_skipped;
    header.dropped_events = 0ul;
    file = CreateFileA(V9X_DP2_RING_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, &header, sizeof(header), &written, 0);
    WriteFile(file, v9x_dp2_ring,
              v9x_dp2_ring_used * (DWORD)sizeof(V9X_DP2R_ENTRY), &written, 0);
    CloseHandle(file);
}

static V9X_DP2R_ENTRY *v9x_dp2_ring_next(v9x_u32 kind)
{
    V9X_DP2R_ENTRY *entry;
    BYTE *bytes;
    DWORD i;

    if (v9x_dp2_ring_used >= V9X_DP2_RING_ENTRIES) {
        v9x_dp2_ring_write();
        return 0;
    }
    entry = &v9x_dp2_ring[v9x_dp2_ring_used++];
    bytes = (BYTE *)entry;
    for (i = 0ul; i < sizeof(*entry); ++i) {
        bytes[i] = 0u;
    }
    entry->kind = kind;
    entry->tsc_in = v9x_rdtsc_low();
    return entry;
}

V9X_DP2R_ENTRY *v9x_dp2_ring_call(v9x_u32 probe_armed)
{
    if (v9x_dp2_ring_phase == 0ul) {
        if (!probe_armed) {
            return 0;
        }
        v9x_dp2_ring = (V9X_DP2R_ENTRY *)VirtualAlloc(
            0, V9X_DP2_RING_ENTRIES * sizeof(V9X_DP2R_ENTRY),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (v9x_dp2_ring == 0) {
            v9x_dp2_ring_phase = 3ul;
            return 0;
        }
        v9x_dp2_ring_skip = (v9x_u32)GetPrivateProfileIntA(
            "Velocity9x", "Dp2RingSkip", 0, "SYSTEM.INI");
        v9x_dp2_ring_delay = (v9x_u32)GetPrivateProfileIntA(
            "Velocity9x", "Dp2RingDelayMs", 0, "SYSTEM.INI");
        v9x_dp2_ring_armed_tick = GetTickCount();
        v9x_dp2_ring_pid = GetCurrentProcessId();
        v9x_dp2_ring_phase = 1ul;
    }
    if (v9x_dp2_ring_phase == 1ul) {
        if (v9x_dp2_ring_skipped < v9x_dp2_ring_skip ||
            GetTickCount() - v9x_dp2_ring_armed_tick < v9x_dp2_ring_delay) {
            ++v9x_dp2_ring_skipped;
            return 0;
        }
        v9x_dp2_ring_phase = 2ul;
        v9x_dp2_ring_live = 1ul;
    }
    if (v9x_dp2_ring_phase != 2ul) {
        return 0;
    }
    return v9x_dp2_ring_next(V9X_DP2R_CALL);
}

static v9x_u32 v9x_dp2_ring_dword(const v9x_u8 *p)
{
    return (v9x_u32)p[0] | ((v9x_u32)p[1] << 8) | ((v9x_u32)p[2] << 16) |
           ((v9x_u32)p[3] << 24);
}

static void v9x_dp2_ring_item(V9X_DP2R_ENTRY *entry, v9x_u32 op,
                              v9x_u32 count, v9x_u32 state, v9x_u32 value)
{
    if (entry->items < V9X_DP2R_ITEMS) {
        V9X_DP2R_ITEM *item = &entry->item[entry->items];

        item->op = (v9x_u8)op;
        item->count = (v9x_u8)(count > 255ul ? 255ul : count);
        item->state = (v9x_u16)state;
        item->value = value;
    }
    ++entry->items;
}

void v9x_dp2_ring_record(V9X_DP2R_ENTRY *entry, v9x_u32 op, v9x_u32 count,
                         const v9x_u8 *payload, v9x_u32 bytes)
{
    v9x_u32 i;

    if (entry == 0) {
        return;
    }
    if (op == 8ul && bytes >= count * 8ul) {
        /* D3DHAL_DP2RENDERSTATE pairs. */
        for (i = 0ul; i < count; ++i) {
            v9x_dp2_ring_item(entry, op, count,
                              v9x_dp2_ring_dword(payload + i * 8ul),
                              v9x_dp2_ring_dword(payload + i * 8ul + 4ul));
        }
        return;
    }
    if (op == 25ul && bytes >= count * 8ul) {
        /* D3DHAL_DP2TEXTURESTAGESTATE: WORD stage, WORD state, value. */
        for (i = 0ul; i < count; ++i) {
            const v9x_u8 *p = payload + i * 8ul;

            v9x_dp2_ring_item(entry, op, count,
                              ((v9x_u32)p[0] << 8) | (v9x_u32)p[2],
                              v9x_dp2_ring_dword(p + 4ul));
        }
        return;
    }
    v9x_dp2_ring_item(entry, op, count, bytes > 0xfffful ? 0xfffful : bytes,
                      bytes >= 4ul ? v9x_dp2_ring_dword(payload) : 0ul);
}

void v9x_dp2_ring_done(V9X_DP2R_ENTRY *entry)
{
    if (entry == 0) {
        return;
    }
    entry->tsc_out = v9x_rdtsc_low();
    if (v9x_dp2_ring_used >= V9X_DP2_RING_ENTRIES) {
        v9x_dp2_ring_write();
    }
}

void v9x_dp2_ring_event_slow(v9x_u32 kind, v9x_u32 a, v9x_u32 b)
{
    V9X_DP2R_ENTRY *entry;

    if (v9x_dp2_ring_phase != 2ul) {
        return;
    }
    entry = v9x_dp2_ring_next(kind);
    if (entry != 0) {
        entry->flags = a;
        entry->command_offset = b;
    }
}

void v9x_dp2_ring_flush(void)
{
    if (v9x_dp2_ring_phase == 2ul) {
        v9x_dp2_ring_write();
    }
}

void v9x_dp2_ring_rearm(void)
{
    /* A second context in the same process keeps the capture it has,
     * written or not: one capture per process. */
    if (v9x_dp2_ring_phase != 0ul &&
        v9x_dp2_ring_pid == GetCurrentProcessId()) {
        return;
    }
    if (v9x_dp2_ring_phase == 2ul) {
        v9x_dp2_ring_write();
    }
    /* The old buffer may be another process's; it goes with that process. */
    v9x_dp2_ring = 0;
    v9x_dp2_ring_used = 0ul;
    v9x_dp2_ring_skipped = 0ul;
    v9x_dp2_ring_live = 0ul;
    v9x_dp2_ring_phase = 0ul;
}
