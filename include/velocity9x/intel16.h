#ifndef VELOCITY9X_INTEL16_H
#define VELOCITY9X_INTEL16_H

/*
 * The Intel family's 16-bit code does not fit in one segment.
 *
 * Win16's 64 KiB limit is per segment, not per image, and the intel-gma
 * family reached it: the eleven Intel units named in the family manifest with
 * CodeSegment = 'I9XXCODE' are compiled into a second CODE segment, leaving
 * _TEXT for everything they share with the other four families
 * (docs\plans\intel-gma950-phase5.md).
 *
 * Open Watcom's compact model compiles every C call near, and a near call
 * cannot cross a segment - it is a wild jump at run time, not a link error.
 * So every call that crosses the boundary must be declared __far on BOTH
 * sides, and this header is the one place that says so. Nothing else may
 * declare a crossing function: a bare extern in a .c file is what a mismatch
 * looks like, and check-tree.ps1 refuses one.
 *
 * The rule that keeps the boundary from being re-broken by accident:
 *
 *   The hook tables are near and live in _TEXT; intel_hw16.c and
 *   intel_bridge16.c own the Intel hook and shared-service bridges; loader.c
 *   also calls v9x_intel_boot_arm_prepare across the segment boundary. Thus a
 *   hook slot never holds the address of a function in another code segment -
 *   it holds a near forwarder that makes the far call.
 *
 * The i9xx_* units are also compiled 32-bit into the host test suite by both
 * Open Watcom and MSVC, where there are no segments and __far does not exist,
 * so the qualifier is spelled through a macro that expands to nothing there.
 */
#if defined(__WATCOMC__) && defined(_M_I86)
#define V9X_I9XX_FAR __far
#else
#define V9X_I9XX_FAR
#endif

/*
 * Calls into I9XXCODE.
 *
 * The four publishers are reached from intel_hw16.c, which owns the near
 * V9X_HW16_OPS table and therefore stays in _TEXT. v9x_intel_boot_arm_prepare
 * is reached from loader.c - the only reference to any moved unit anywhere
 * outside the Intel files. v9x_i9xx_sandbox_calculate is reached from
 * gma950_hw16.c, which owns the near V9X_HW16_DEVICE table.
 */
void V9X_I9XX_FAR v9x_intel_publish_mmio_fingerprint(void);
void V9X_I9XX_FAR v9x_intel_publish_gtt_inventory(void);
void V9X_I9XX_FAR v9x_intel_publish_ring_plan(void);
void V9X_I9XX_FAR v9x_intel_publish_event(unsigned short kind,
                                          unsigned short context);
void V9X_I9XX_FAR v9x_intel_boot_arm_prepare(void);

/*
 * Calls out of I9XXCODE.
 *
 * Both are shared with every family, so both stay near in _TEXT and are
 * reached through the far wrappers in src\display16\intel_bridge16.c. The
 * wrappers, not the originals, are what the moved units call.
 */
/*
 * The qualifier sits immediately before the name in both, so that it reads as
 * "far function", not "returns a far pointer". Data pointers need no
 * qualifier: the compact model already addresses all data far.
 */
struct v9x_build_identity;
const struct v9x_build_identity *V9X_I9XX_FAR
    v9x_intel_bridge_build_identity(void);
unsigned short V9X_I9XX_FAR v9x_intel_bridge_mode_geometry(
    unsigned short *width, unsigned short *height,
    unsigned short *bpp, unsigned short *pitch);

/*
 * Project-local string helpers.
 *
 * strcmp, strcpy, strlen and strcat live in clibc.lib, which declares segment
 * _TEXT, and there is no linker mechanism to place a second copy in I9XXCODE.
 * They were referenced only by intel_exec16.c and intel_boot16.c - both moved
 * units - so replacing them here removes those library objects from the link
 * entirely. Defined in src\display16\intel_str16.c, which is itself moved, so
 * these calls are near.
 */
unsigned short v9x_intel_str_length(const char *text);
unsigned short v9x_intel_str_equal(const char *left, const char *right);
void v9x_intel_str_copy(char *destination, const char *source);
void v9x_intel_str_append(char *destination, const char *source);

#endif /* VELOCITY9X_INTEL16_H */
