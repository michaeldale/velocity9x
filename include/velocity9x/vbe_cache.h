/*
 * The C view of the mini-VDD's VBE cache contract.
 *
 * include\asm\V9XMAPI.INC is the same contract for the two assembly users that
 * actually speak the API. This header exists because the consumers of what
 * comes back - the admission policy, the diagnostic dump, the mode inventory -
 * are C, and they need the same bounds and the same flag meanings to validate
 * a count the API reported (invariant 4 of docs\plans\dynamic-vbe-pipeline.md:
 * every count exposed across an ABI is validated again by its consumer).
 *
 * Two files for one contract is a duplication, and it is deliberate: no
 * assembler and no C compiler in this project can read the other's syntax.
 * What makes it safe is that the numbers are asserted equal by
 * scripts\check-tree.ps1, which fails the tree check rather than letting the
 * two drift into a boot-time misread.
 *
 * Nothing here calls the BIOS, allocates, or touches the OS, so the host suite
 * gets it for free.
 */
#ifndef VELOCITY9X_VBE_CACHE_H
#define VELOCITY9X_VBE_CACHE_H

#include "velocity9x/types.h"

/* Contract versions. v1 is the shipping API; v2 adds indexed enumeration, the
 * VBE 3 linear stride and colour fields, and EDID chunks. A display driver
 * refuses a contract newer than the one it was built against rather than
 * guessing at a register layout. */
#define V9X_VBE_API_V1 ((v9x_u16)1u)
#define V9X_VBE_API_V2 ((v9x_u16)2u)

/*
 * Bounds. Separate limits so one corrupt BIOS field cannot become an
 * unbounded boot delay or an overwrite.
 */
#define V9X_VBE_MODE_LIST_MAX      ((v9x_u16)128u)
#define V9X_VBE_MODE_QUERY_MAX     ((v9x_u16)128u)
#define V9X_VBE_CACHE_MAX          ((v9x_u16)64u)
#define V9X_VBE_BASELINE_PROBE_MAX ((v9x_u16)16u)

/* EDID block 0 only, delivered in register-only chunks. */
#define V9X_VBE_EDID_BYTES       ((v9x_u16)128u)
#define V9X_VBE_EDID_CHUNK_BYTES ((v9x_u16)16u)
#define V9X_VBE_EDID_CHUNKS      ((v9x_u16)8u)

/*
 * Per-record flags.
 *
 * ORIGIN_LIST and ORIGIN_PROBE separate a mode the BIOS listed from one the
 * mini-VDD probed only so the static path could find its aperture. Only
 * list-derived records may become runtime rows; a rescue probe that leaked
 * into enumeration would let an invalid mode list contribute modes after all.
 *
 * MASKS_LINEAR and MASKS_LEGACY record where the channel layout came from, so
 * a transposed channel can be traced to the fallback rather than guessed at.
 */
#define V9X_VBE_RF_ORIGIN_LIST  ((v9x_u16)0x0001u)
#define V9X_VBE_RF_ORIGIN_PROBE ((v9x_u16)0x0002u)
#define V9X_VBE_RF_MASKS_LINEAR ((v9x_u16)0x0004u)
#define V9X_VBE_RF_MASKS_LEGACY ((v9x_u16)0x0008u)
#define V9X_VBE_RF_LIN_STRIDE   ((v9x_u16)0x0010u)

/*
 * Status flags, as reported by the STATUS function.
 *
 * "Zero cached records" has several causes with different fixes, so the state
 * is reported as flags plus counts rather than one validity bit.
 */
#define V9X_VBE_ST_CTRL_VALID     ((v9x_u16)0x0001u)
#define V9X_VBE_ST_LIST_VALID     ((v9x_u16)0x0002u)
#define V9X_VBE_ST_LIST_TERM      ((v9x_u16)0x0004u)
#define V9X_VBE_ST_LIST_OVERFLOW  ((v9x_u16)0x0008u)
#define V9X_VBE_ST_LIST_FLAGGED   ((v9x_u16)0x0010u)
#define V9X_VBE_ST_LIST_UNREACHED ((v9x_u16)0x0020u)
#define V9X_VBE_ST_CACHE_FULL     ((v9x_u16)0x0040u)
#define V9X_VBE_ST_QUERY_FAILED   ((v9x_u16)0x0080u)
#define V9X_VBE_ST_EDID_VALID     ((v9x_u16)0x0100u)
#define V9X_VBE_ST_EDID_NO_DDC    ((v9x_u16)0x0200u)
#define V9X_VBE_ST_EDID_FAILED    ((v9x_u16)0x0400u)
#define V9X_VBE_ST_COLLECT_OFF    ((v9x_u16)0x0800u)

/*
 * How many cached records a consumer may read, given the reported status and
 * count. Zero when the collection never ran, the controller block was not
 * credible or the mode list could not be trusted; otherwise the count, clamped
 * to the cache bound - a reported figure above the bound is a contract
 * disagreement, and the safe reading of one is always the smaller number.
 */
v9x_u16 v9x_vbe_scan_usable_count(v9x_u16 status, v9x_u16 cached_count);

/*
 * May this scan contradict a family baseline row - that is, may a baseline row
 * the scan does not corroborate be hidden from GDI, DirectDraw and the mode
 * inventory?
 *
 * Stricter than v9x_vbe_scan_usable_count, and deliberately: admitting a mode
 * wrongly offers something that fails to set, while hiding one wrongly takes
 * away something that works. So this additionally requires the walk to have
 * been complete - terminator seen, nothing truncated, nothing overflowed,
 * nothing refused. Anything less publishes every baseline row exactly as the
 * static driver does today.
 *
 * A valid, complete, empty cache does qualify: it contradicts nothing, because
 * the caller compares rows against the records it actually has.
 */
v9x_u16 v9x_vbe_scan_may_contradict(v9x_u16 status);

#endif /* VELOCITY9X_VBE_CACHE_H */
