/*
 * VBE BIOS services. Ported from V9xSetVbeMode and V9xSetMatroxScanLinePitch
 * in runtime.asm with no behavioural change.
 *
 * INT 10h goes through #pragma aux rather than an assembly helper so each call
 * stays one near function. Only 16-bit registers are touched, which matters:
 * the C here is compiled for 8086 while runtime.asm is .386p, so anything
 * needing a 32-bit register has to stay in assembly.
 */
#include "velocity9x/vbe16.h"

/* 4F02h. Returns AX; 004Fh is success. BX carries the mode plus the family's
 * flags, so the no-clear (8000h) and linear-framebuffer (4000h) bits are data
 * rather than a build-time choice. */
static unsigned short v9x_int10_set_mode(unsigned short ax, unsigned short bx);
#pragma aux v9x_int10_set_mode =        \
    "int 10h"                           \
    parm [ax] [bx]                      \
    value [ax]                          \
    modify [ax bx cx dx si di es]

unsigned short v9x_vbe_set_mode(unsigned short mode, unsigned short mode_flags)
{
    return v9x_int10_set_mode(0x4f02u, (unsigned short)(mode | mode_flags))
               == 0x004fu ? 1u : 0u;
}

/*
 * 4F00h and 4F01h, the buffered calls.
 *
 * These cannot be a plain "int 10h" the way the mode set is. They hand the
 * BIOS a buffer in ES:DI, and this code runs in 16-bit protected mode, so the
 * selector would reach the V86 BIOS as a raw paragraph address and it would
 * fill in whatever memory happens to live there. The buffer therefore has to
 * be real DOS memory (DPMI 0100h), and the call has to be made by asking the
 * DPMI host to simulate the interrupt in real mode (DPMI 0300h).
 *
 * DPMI 0100h rather than GlobalDosAlloc because this file sits outside the
 * driver's OS boundary: check-tree.ps1 keeps <windows.h> to six files, and
 * widening that to reach one allocator would trade a real architectural line
 * for nothing. INT 31h through #pragma aux is the idiom the mode set already
 * uses, and needs no 32-bit register either.
 *
 * Parsing the answers is not here. src\common\vbe_parse.c owns that, because
 * "is this block credible and does it describe the mode we just set" is a
 * decision worth testing on the host, where a wrong answer costs nothing.
 */

/*
 * The DPMI real-mode call structure, 50 bytes. Every field is a 16-bit half so
 * the layout cannot depend on the compiler's struct packing and no 32-bit
 * arithmetic creeps in: the halves this code sets are all low words.
 */
struct v9x_dpmi_real_regs {
    v9x_u16 edi_lo, edi_hi;
    v9x_u16 esi_lo, esi_hi;
    v9x_u16 ebp_lo, ebp_hi;
    v9x_u16 reserved_lo, reserved_hi;
    v9x_u16 ebx_lo, ebx_hi;
    v9x_u16 edx_lo, edx_hi;
    v9x_u16 ecx_lo, ecx_hi;
    v9x_u16 eax_lo, eax_hi;
    v9x_u16 flags;
    v9x_u16 es, ds, fs, gs;
    v9x_u16 ip, cs, sp, ss;
};

/*
 * DPMI 0100h, allocate a DOS memory block. BX is paragraphs; on success AX is
 * the real-mode segment and DX the protected-mode selector.
 *
 * The carry flag is folded into the result branchlessly - sbb/not/and rather
 * than a jump - so the pragma needs no label: a failed call returns zero, and
 * selector zero is never valid.
 */
static unsigned long v9x_dpmi_alloc_dos_block(unsigned short paragraphs);
#pragma aux v9x_dpmi_alloc_dos_block =  \
    "mov ax,0100h"                      \
    "int 31h"                           \
    "sbb cx,cx"                         \
    "not cx"                            \
    "and ax,cx"                         \
    "and dx,cx"                         \
    parm [bx]                           \
    value [dx ax]                       \
    modify [ax bx cx dx]

/*
 * DPMI 0300h, simulate a real-mode interrupt. BL is the interrupt number and
 * BH must be zero, so the caller passes the number as the whole of BX. CX is
 * the count of stack words to copy, which is none. ES:DI addresses the
 * structure above - an ordinary protected-mode pointer, unlike the buffer it
 * names inside. Returns 1 on success.
 */
static unsigned short v9x_dpmi_simulate_int(
    unsigned short interrupt_number, struct v9x_dpmi_real_regs __far *regs);
#pragma aux v9x_dpmi_simulate_int =     \
    "mov ax,0300h"                      \
    "xor cx,cx"                         \
    "int 31h"                           \
    "sbb ax,ax"                         \
    "inc ax"                            \
    parm [bx] [es di]                   \
    value [ax]                          \
    modify [ax bx cx dx es di]

/* One block serves both calls: they are sequential and never nested. 512 bytes
 * is what 4F00h may write, and 32 paragraphs is that. */
#define V9X_VBE_BLOCK_PARAGRAPHS ((unsigned short)32u)

static v9x_u16 v9x_vbe_block_segment = 0u;
static v9x_u16 v9x_vbe_block_selector = 0u;
static struct v9x_dpmi_real_regs __far v9x_vbe_regs;

/*
 * Allocated once and never freed. That matches how the driver already treats
 * its framebuffer selector: Disable leaves it alone, because a stale copy
 * anywhere else would dangle, and the enable/disable cycle would otherwise
 * churn DOS memory on every mode switch.
 */
static v9x_u16 v9x_vbe_ensure_block(void)
{
    unsigned long allocation;

    if (v9x_vbe_block_selector != 0u) {
        return 1u;
    }
    allocation = v9x_dpmi_alloc_dos_block(V9X_VBE_BLOCK_PARAGRAPHS);
    if (allocation == 0ul) {
        return 0u;
    }
    v9x_vbe_block_segment = (v9x_u16)allocation;
    v9x_vbe_block_selector = (v9x_u16)(allocation >> 16);
    return 1u;
}

static v9x_u8 __far *v9x_vbe_block_pointer(void)
{
    union {
        v9x_u8 __far *pointer;
        struct {
            v9x_u16 offset;
            v9x_u16 segment;
        } parts;
    } reference;

    reference.parts.offset = 0u;
    reference.parts.segment = v9x_vbe_block_selector;
    return reference.pointer;
}

static void v9x_vbe_zero(v9x_u8 __far *block, v9x_u16 count)
{
    v9x_u16 index;

    for (index = 0u; index < count; ++index) {
        block[index] = 0u;
    }
}

/* Returns 1 when the BIOS answered 004Fh. */
static v9x_u16 v9x_vbe_buffered_call(v9x_u16 function, v9x_u16 argument)
{
    v9x_vbe_zero((v9x_u8 __far *)&v9x_vbe_regs,
                 (v9x_u16)sizeof(v9x_vbe_regs));
    v9x_vbe_regs.eax_lo = function;
    v9x_vbe_regs.ecx_lo = argument;
    v9x_vbe_regs.es = v9x_vbe_block_segment;
    v9x_vbe_regs.edi_lo = 0u;
    /* SS:SP left zero asks the DPMI host to supply the real-mode stack. */

    if (v9x_dpmi_simulate_int(0x0010u, &v9x_vbe_regs) == 0u) {
        return 0u;
    }
    return v9x_vbe_regs.eax_lo == 0x004fu ? 1u : 0u;
}

unsigned short v9x_vbe_read_controller_info(
    struct v9x_vbe_controller_summary *out)
{
    v9x_u8 __far *block;

    if (out == 0) {
        return 0u;
    }
    if (v9x_vbe_ensure_block() == 0u) {
        return 0u;
    }

    block = v9x_vbe_block_pointer();
    v9x_vbe_zero(block, V9X_VBE_CONTROLLER_BLOCK_BYTES);
    /* Stamping "VBE2" is what asks for the 2.0 block shape rather than the
     * 1.x one. The BIOS overwrites it with "VESA" when it answers, which is
     * how the parser tells a real answer from an untouched buffer. */
    block[0] = (v9x_u8)'V';
    block[1] = (v9x_u8)'B';
    block[2] = (v9x_u8)'E';
    block[3] = (v9x_u8)'2';

    if (v9x_vbe_buffered_call(0x4f00u, 0u) == 0u) {
        return 0u;
    }
    return v9x_vbe_parse_controller_info(block, out);
}

unsigned short v9x_vbe_read_mode_info(unsigned short mode,
                                      struct v9x_vbe_mode_summary *out)
{
    v9x_u8 __far *block;

    if (out == 0) {
        return 0u;
    }
    if (v9x_vbe_ensure_block() == 0u) {
        return 0u;
    }

    block = v9x_vbe_block_pointer();
    v9x_vbe_zero(block, V9X_VBE_MODE_BLOCK_BYTES);
    if (v9x_vbe_buffered_call(0x4f01u, mode) == 0u) {
        return 0u;
    }
    return v9x_vbe_parse_mode_info(block, out);
}

/*
 * The mode set and the two readers live here; 4F06h does not.
 *
 * VBE 4F06h is used by exactly one family, and this object is linked into all
 * of them. Putting it here would place its signature in every binary and make
 * the cross-family audit unable to tell the Millennium II path from the S3
 * one, so it lives in that family's own chip module instead.
 *
 * The same reasoning is why the vbe family declares no required instruction
 * patterns of its own: 4F00h and 4F01h are above, in shared code, so their
 * immediates appear in the S3 and Matrox images too and cannot identify
 * anything. See docs\decisions\2026-08-16-vbe-tier0-family.md.
 */
