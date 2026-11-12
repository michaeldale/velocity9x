#ifndef VELOCITY9X_LEAF16_H
#define VELOCITY9X_LEAF16_H

/*
 * LEAFCODE: a second code segment for leaf modules of the 16-bit driver.
 *
 * Win16's 64 KiB limit is per segment, and the ati family's _TEXT passed
 * its 57,344-byte budget when the screen shadow went in (2026-11-12:
 * 61,155 bytes). A family that names a leaf module in its manifest with
 * CodeSegment = 'LEAFCODE' compiles it there: modules called from a few
 * places, which call nothing in _TEXT but runtime.asm's FAR entries. They
 * are src\common\mtrr.c and edid.c, and the screen shadow's shadow.c and
 * shadow16.c, which call each other near and so must share the segment.
 *
 * Open Watcom's compact model compiles every C call near, and a near call
 * cannot cross a segment (velocity9x\intel16.h says why that is a wild
 * jump at run time, not a link error). So every function called from
 * outside the segment is declared V9X_LEAF_FAR, in its header and at its
 * definition. A family that leaves the module in _TEXT still works: a far
 * call to the caller's own segment is only slower.
 *
 * The same sources build 32-bit (the HAL, the mini-VDD, probes, the host
 * tests), where there are no segments, so the qualifier is spelled
 * through a macro that expands to nothing there.
 */
#if defined(__WATCOMC__) && defined(_M_I86)
#define V9X_LEAF_FAR __far
#else
#define V9X_LEAF_FAR
#endif

/*
 * A 16 x 16 to 32-bit unsigned multiply, for code in LEAFCODE.
 *
 * In the compact model a 32-bit multiply is a near call to __U4M, and a
 * 32-bit divide or remainder to __U4D, both in clibc.lib, which declares
 * _TEXT; there is no far copy, so a module that calls one cannot leave
 * _TEXT (wlink E2052; docs\plans\intel-gma950-phase5.md found the same for
 * I9XXCODE). Every multiply in the leaf modules has operands that fit 16
 * bits - coordinates, pitches, heights, an EDID clock count - so on 16-bit
 * Watcom it is the one MUL instruction, inline, and plain C elsewhere.
 * Both forms take the low 16 bits of each operand.
 */
#if defined(__WATCOMC__) && defined(_M_I86)
unsigned long v9x_leaf_mul16(unsigned short a, unsigned short b);
#pragma aux v9x_leaf_mul16 = \
    "mul dx" \
    parm [ax] [dx] \
    value [dx ax] \
    modify exact [ax dx] nomemory;
#define V9X_LEAF_MUL16(a, b) \
    v9x_leaf_mul16((unsigned short)(a), (unsigned short)(b))
#else
#define V9X_LEAF_MUL16(a, b) \
    ((unsigned long)(unsigned short)(a) * (unsigned long)(unsigned short)(b))
#endif

#endif
