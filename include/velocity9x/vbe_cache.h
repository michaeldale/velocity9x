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

/* Contract versions. v1 and v2 are retained as history. v2 added indexed
 * enumeration, VBE 3 linear stride and colour fields, and EDID chunks. v3 adds
 * the Intel-only read-only MMIO fingerprint functions. A display driver
 * refuses any other contract rather than guessing at a register layout. */
#define V9X_VBE_API_V1 ((v9x_u16)1u)
#define V9X_VBE_API_V2 ((v9x_u16)2u)
#define V9X_VBE_API_V3 ((v9x_u16)3u)
#define V9X_VBE_API_V4 ((v9x_u16)4u)
#define V9X_VBE_API_V5 ((v9x_u16)5u)
#define V9X_VBE_API_V6 ((v9x_u16)6u)
/* v7 adds the Intel-only read-only reserve hash. Read-only, so it ships in
 * the unarmed build. See includesm\V9XMAPI.INC. */
#define V9X_VBE_API_V7 ((v9x_u16)7u)

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
/* The record exists only because the mode was set first: see the sweep in
 * src\minivdd32\loader.asm. Judged by content like any other. */
#define V9X_VBE_RF_ORIGIN_SWEEP ((v9x_u16)0x0020u)

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
#define V9X_VBE_ST_QUERY_LIMIT    ((v9x_u16)0x1000u)
#define V9X_VBE_ST_SWEEP_RAN      ((v9x_u16)0x2000u)
#define V9X_VBE_ST_SWEEP_UNRESTORED ((v9x_u16)0x4000u)
/* A listed 4F01h answered exactly 014Fh, the standard's "function call
 * failed": for a VBE 1.2 BIOS, a mode it cannot set (VBE 1.2, 6.2).
 * QUERY_FAILED is every other answer that is not 004Fh - a BIOS that does
 * not handle 4F01h at all (AL not 4Fh) among them - and keeps its meaning,
 * an incomplete walk, at every version. */
#define V9X_VBE_ST_QUERY_UNAVAIL  ((v9x_u16)0x8000u)

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

/*
 * v9x_vbe_scan_may_contradict for a controller of the given VBE version.
 *
 * Identical for 2.0 and later, where QUERY_UNAVAIL withdraws the scan's
 * authority as QUERY_FAILED does. For a 1.x controller a listed mode whose
 * 4F01h answered 014Fh (QUERY_UNAVAIL) does not: VBE 1.2 (section 6.2)
 * says that some listed modes "may not be available due to the video board's
 * current memory and monitor configuration" and leaves it to the application
 * to verify each through 4F01h, so a 1.2 BIOS answering 014Fh is reporting a
 * mode it cannot set, not an incomplete walk. Measured on a 2 MiB Trio64
 * whose BIOS lists 0x0118-0x011B, answers 014Fh to 4F01h for them and refuses
 * them at 4F02h too (2026-08-26-s3-physical-pipeline-inert.md, section 7).
 * Any other refusal (QUERY_FAILED) still withdraws it, and every other
 * completeness rule still applies.
 */
v9x_u16 v9x_vbe_scan_may_contradict_version(v9x_u16 status, v9x_u16 version);

#endif /* VELOCITY9X_VBE_CACHE_H */
