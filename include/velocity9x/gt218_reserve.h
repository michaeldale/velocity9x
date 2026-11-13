/*
 * Where the GT218's 2D engine keeps its channel: a reserve at the top of
 * the driver's BAR1 mapping, taken off the DirectDraw heap. Pure
 * arithmetic, shared by the 16-bit row (gt218_hw16.c) and the host tests.
 */
#ifndef VELOCITY9X_GT218_RESERVE_H
#define VELOCITY9X_GT218_RESERVE_H

/*
 * The reserve's size: nv50_chan.h's region (B0000h bytes: instance block,
 * page table, runlist, IB ring, pushbuffer, context image) rounded up to
 * a megabyte. It starts on a megabyte boundary, so it is 64 KiB aligned
 * as the channel's page table needs.
 */
#define V9X_GT218_RESERVE_BYTES 0x00100000ul
/* The channel's VM maps the first 512 MiB of video memory, and the
 * reserve must lie inside it (nv50_chan.h, V9X_NV50_VM_BYTES). */
#define V9X_GT218_RESERVE_VM_BYTES 0x20000000ul

/*
 * The offset in video memory at which the reserve starts, which is also
 * where the heap must end, given the bytes usable through the mapping and
 * the bytes the visible screen takes. 0 when there is no room: the
 * mapping is smaller than the reserve, the reserve would overlap the
 * screen, or it would fall outside the channel's VM. The caller then
 * leaves the heap whole and claims no engine.
 */
unsigned long v9x_gt218_reserve_start(unsigned long usable_bytes,
                                      unsigned long visible_bytes);

#endif
