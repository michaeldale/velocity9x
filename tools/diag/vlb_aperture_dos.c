/*
 * Velocity9x VLB linear-aperture probe.
 *
 * This is not the survey, and it must never be handed to a stranger.
 *
 * V9XSURV takes the whole picture and writes nothing that outlives the run.
 * This tool answers one question the survey structurally cannot: does the
 * machine decode anything at the address the card's linear window claims? On
 * the PCI parts that window is a BAR the host bridge routes; on a 486 with a
 * VESA Local Bus card there is no host bridge, the position lives in
 * CR58/CR59/CR5A, and the 486 chipset has to decode it. Nothing measured so far
 * says whether it does.
 *
 * Two things make this a separate tool rather than a fourth switch on the
 * survey:
 *
 *   It has to reach above 16 MB. The survey used INT 15h AH=87h, a BIOS
 *   service with a 24-bit descriptor base, and both candidate addresses on the
 *   machine we have - the BIOS's 7F000000h and the S3VBE TSR's 04000000h - are
 *   past that ceiling. Reaching them needs unreal mode: a brief excursion into
 *   protected mode to load FS with a 4 GB limit, then back to real mode with
 *   that limit still cached. That is the genuinely risky machinery here, and it
 *   is why /selftest below is not optional.
 *
 *   It has to set a mode. The survey's own recorded caveat is that on these
 *   parts the window may only answer once a mode has been set with linear
 *   addressing enabled - so a probe that sets no mode can never return a
 *   conclusive negative. Setting one is forbidden in the survey by a build-time
 *   gate, for good reasons that all still hold. So this lives elsewhere.
 *
 * How it avoids answering the wrong question:
 *
 *   A dead window and a broken flat read look identical - both give FFh. So
 *   every run first reads the system BIOS ROM at F0000h twice, once through an
 *   ordinary real-mode far pointer and once through the flat segment, and
 *   refuses to report anything else unless the two agree. Unreal mode is proved
 *   working on known data before it is trusted on unknown data.
 *
 *   All-FFh at an address is weak evidence. So /pattern writes a recognisable
 *   32-byte marker into video memory through the banked A0000h window - the
 *   path every VBE 1.2 application already uses, so it is known to work - reads
 *   it back through that same window to prove the write landed, and only then
 *   looks for it at the linear base. A match is positive identification: the
 *   two windows are the same memory. Absence of the marker is still only
 *   suggestive, and the report says so.
 *
 * Escalation, in the order the risk increases. Each step's report is closed on
 * disk before the next begins.
 *
 *   V9XAPER                              read-only apart from the documented
 *                                        CR38/CR39 unlock the survey already
 *                                        performs; no mode set, no window
 *                                        writes. Proves unreal mode works and
 *                                        says what is at the window's address.
 *   V9XAPER /pattern                     adds a mode set and a marker in VRAM.
 *   V9XAPER /pattern /enable             adds CR58[4], linear addressing on, at
 *                                        the base the BIOS chose.
 *   V9XAPER /pattern /relocate:04000000 /enable
 *                                        moves the window first.
 *
 * Everything written is restored: the video mode, CR58/CR59/CR5A, CR38/CR39,
 * A20, and FS's segment limit. Nothing here survives a reboot either way.
 */

#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_PROBE_SCHEMA "1"
#define V9X_PROBE_DEFAULT "C:\\V9XAPER.INI"

/* 32 bytes is enough to be unmistakable and small enough that the marker is a
 * few pixels in the corner of the screen rather than a visible band. */
#define V9X_MARKER_BYTES 32

/* The self-test target: the system BIOS ROM. Always present, never all-FFh, and
 * reachable both ways - which is the whole point of it. */
#define V9X_SELFTEST_LINEAR 0x000f0000ul
#define V9X_SELFTEST_SEGMENT 0xf000u

static unsigned char gdt[16];
static unsigned char gdtr[6];
static unsigned char marker[V9X_MARKER_BYTES];
static unsigned char readback[V9X_MARKER_BYTES];
static unsigned char flat_bytes[V9X_MARKER_BYTES];

static FILE *report;
static char report_path[128];
static int windows_present;
static int cpu_386;
static int in_v86;
static int unreal_active;
static int mode_changed;
static int window_written;
static unsigned char saved_break_state;
static unsigned char saved_cr58;
static unsigned char saved_cr59;
static unsigned char saved_cr5a;
static unsigned char saved_lock1;
static unsigned char saved_lock2;

static int want_pattern;
static int want_linear;
static int want_write;
static int want_enable;
static int want_relocate;
static unsigned long relocate_base;
static int have_extra_base;
static unsigned long extra_base;
static int window_saved;
static unsigned long top_of_ram;

static const char hex_digits[] = "0123456789ABCDEF";

/* ------------------------------------------------------------------ */
/* Report writing                                                      */
/* ------------------------------------------------------------------ */

static void wr_section(const char *name)
{
    fprintf(report, "\n[%s]\n", name);
}

static void wr_str(const char *key, const char *value)
{
    fprintf(report, "%s=%s\n", key, value);
}

static void wr_x8(const char *key, unsigned char value)
{
    fprintf(report, "%s=%02X\n", key, value);
}

static void wr_x16(const char *key, unsigned short value)
{
    fprintf(report, "%s=%04X\n", key, value);
}

static void wr_x32(const char *key, unsigned long value)
{
    fprintf(report, "%s=%08lX\n", key, value);
}

static void wr_status(const char *value)
{
    wr_str("Status", value);
}

static void wr_bytes(const char *key, const unsigned char *data, unsigned count)
{
    char line[80];
    unsigned at = 0u;
    unsigned index;

    for (index = 0u; index < count; ++index) {
        line[at++] = hex_digits[(data[index] >> 4) & 0x0fu];
        line[at++] = hex_digits[data[index] & 0x0fu];
    }
    line[at] = '\0';
    fprintf(report, "%s=%s\n", key, line);
}

/* True when every byte is the same - which is what a dead address looks like,
 * and what a live one almost never does. */
static int all_same(const unsigned char *data, unsigned count,
                    unsigned char *value)
{
    unsigned index;

    *value = data[0];
    for (index = 1u; index < count; ++index) {
        if (data[index] != data[0]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* CPU gate                                                            */
/* ------------------------------------------------------------------ */

/*
 * The same staircase the survey uses, and for the same reason: the 32-bit
 * encodings below must not be executed on a CPU that has no such opcodes. The
 * difference is what happens on failure. The survey degrades and carries on;
 * this tool stops, because everything it does is built on those encodings.
 */
static unsigned short flags_probe(void);
#pragma aux flags_probe =   \
    "pushf"                 \
    "pop  cx"               \
    "mov  ax,cx"            \
    "and  ax,0fffh"         \
    "push ax"               \
    "popf"                  \
    "pushf"                 \
    "pop  ax"               \
    "push cx"               \
    "popf"                  \
    value [ax] modify exact [ax cx];

static unsigned short flags_set_probe(void);
#pragma aux flags_set_probe = \
    "pushf"                   \
    "pop  cx"                 \
    "mov  ax,cx"              \
    "or   ax,7000h"           \
    "push ax"                 \
    "popf"                    \
    "pushf"                   \
    "pop  ax"                 \
    "push cx"                 \
    "popf"                    \
    value [ax] modify exact [ax cx];

/* SMSW AX as raw bytes; Open Watcom's inline assembler will not take the
 * mnemonic for a 16-bit DOS target. Bit 0 is CR0.PE, set in virtual-8086 mode. */
static unsigned short machine_status_word(void);
#pragma aux machine_status_word = \
    "db 0x0f, 0x01, 0xe0"         \
    value [ax] modify exact [ax];

/* ------------------------------------------------------------------ */
/* Unreal mode                                                         */
/* ------------------------------------------------------------------ */

/*
 * Load FS with a descriptor whose limit is 4 GB, then leave protected mode.
 *
 * The segment register keeps the descriptor it was given until something writes
 * to it again - the real-mode 64 KB limit is only reloaded on a write - so a
 * 32-bit offset through FS reaches any physical address afterwards. That is the
 * whole trick, and it is why leave_unreal exists: writing 0 to FS puts the
 * ordinary limit back rather than leaving DOS with a segment that can address
 * the whole machine.
 *
 * Interrupts are off across the excursion, and nothing between setting PE and
 * clearing it touches memory - the LGDT that does is issued while still in real
 * mode, where DS means what it normally means. A maskable interrupt arriving
 * with our GDT loaded and the real-mode IDT still in place is the failure this
 * guards against; NMI in that window is a residual risk that CLI cannot cover
 * and that no version of this technique can.
 */
static void enter_unreal(void *descriptor);
#pragma aux enter_unreal =      \
    ".386p"                     \
    "pushf"                     \
    "cli"                       \
    "lgdt fword ptr [si]"       \
    "mov  eax,cr0"              \
    "or   al,1"                 \
    "mov  cr0,eax"              \
    "jmp  short L1"             \
    "L1:"                       \
    "mov  bx,8"                 \
    "mov  fs,bx"                \
    "mov  eax,cr0"              \
    "and  al,0feh"              \
    "mov  cr0,eax"              \
    "jmp  short L2"             \
    "L2:"                       \
    "popf"                      \
    parm [si] modify exact [ax bx];

static unsigned long read_flat(unsigned long linear);
#pragma aux read_flat =  \
    ".386"               \
    "movzx eax,ax"       \
    "movzx edx,dx"       \
    "shl   edx,16"       \
    "or    eax,edx"      \
    "mov   esi,eax"      \
    "mov   eax,fs:[esi]" \
    "mov   edx,eax"      \
    "shr   edx,16"       \
    parm [dx ax] value [dx ax] modify exact [ax dx si];

/*
 * The counterpart, for the one case the banked cross-check cannot cover.
 *
 * In a linear framebuffer mode the A0000h window is generally gone - that is
 * what linear addressing is for - so the marker cannot be placed through a path
 * already known to work. Instead the marker is written and read back through
 * the flat segment itself. A dead address swallows the write and reads back
 * FFh, so the round trip still distinguishes live from dead; what it cannot do
 * is tell video memory from some other decoder.
 *
 * That gap is closed by arithmetic rather than by hope: the caller refuses to
 * write unless the target is above the top of installed RAM, which on a machine
 * with no PCI and no other decoders leaves video memory or nothing.
 */
static void write_flat(unsigned long linear, unsigned long value);
#pragma aux write_flat =      ".386"                    "movzx eax,ax"            "movzx edx,dx"            "shl   edx,16"            "or    eax,edx"           "mov   esi,eax"           "movzx eax,bx"            "movzx edx,cx"            "shl   edx,16"            "or    eax,edx"           "mov   fs:[esi],eax"      parm [dx ax] [cx bx] modify exact [ax dx si];

static void leave_unreal(void);
#pragma aux leave_unreal = \
    ".386"                 \
    "xor  ax,ax"           \
    "mov  fs,ax"           \
    modify exact [ax];

/*
 * Selector 08h: base 0, limit 4 GB, 32-bit writable data. The pseudo-descriptor
 * LGDT reads needs the GDT's *linear* address, which in the small model means
 * building it from DS rather than from a pointer.
 */
static void build_gdt(void)
{
    struct SREGS segments;
    unsigned long linear;

    segread(&segments);
    linear = ((unsigned long)segments.ds << 4) +
             (unsigned long)FP_OFF((void far *)gdt);

    memset(gdt, 0, sizeof(gdt));
    gdt[8] = 0xffu;         /* limit 15:0                                */
    gdt[9] = 0xffu;
    gdt[10] = 0x00u;        /* base 15:0                                 */
    gdt[11] = 0x00u;
    gdt[12] = 0x00u;        /* base 23:16                                */
    gdt[13] = 0x92u;        /* present, ring 0, data, writable           */
    gdt[14] = 0xcfu;        /* 4 KB granularity, 32-bit, limit 19:16 = F */
    gdt[15] = 0x00u;        /* base 31:24                                */

    gdtr[0] = (unsigned char)(sizeof(gdt) - 1u);
    gdtr[1] = 0x00u;
    gdtr[2] = (unsigned char)(linear & 0xfful);
    gdtr[3] = (unsigned char)((linear >> 8) & 0xfful);
    gdtr[4] = (unsigned char)((linear >> 16) & 0xfful);
    gdtr[5] = (unsigned char)((linear >> 24) & 0xfful);

    wr_x32("GdtLinear", linear);
}

static void write_flat_bytes(unsigned long linear, const unsigned char *data,
                             unsigned count)
{
    unsigned offset;

    for (offset = 0u; offset < count; offset += 4u) {
        unsigned long value = (unsigned long)data[offset] |
                              ((unsigned long)data[offset + 1u] << 8) |
                              ((unsigned long)data[offset + 2u] << 16) |
                              ((unsigned long)data[offset + 3u] << 24);
        write_flat(linear + offset, value);
    }
}

static void read_flat_bytes(unsigned long linear, unsigned char *out,
                            unsigned count)
{
    unsigned offset;

    for (offset = 0u; offset < count; offset += 4u) {
        unsigned long value = read_flat(linear + offset);

        out[offset] = (unsigned char)(value & 0xfful);
        out[offset + 1u] = (unsigned char)((value >> 8) & 0xfful);
        out[offset + 2u] = (unsigned char)((value >> 16) & 0xfful);
        out[offset + 3u] = (unsigned char)((value >> 24) & 0xfful);
    }
}

/*
 * Prove the flat read before trusting it.
 *
 * The system BIOS ROM is read twice - once as F000:0000 through an ordinary far
 * pointer, once as linear F0000h through FS - and the two must agree. Without
 * this, "all FFh at the aperture" would be indistinguishable from "unreal mode
 * is not working", and that is precisely the wrong conclusion to draw
 * confidently.
 */
static int unreal_self_test(void)
{
    const unsigned char far *rom =
        (const unsigned char far *)MK_FP(V9X_SELFTEST_SEGMENT, 0u);
    unsigned char through_far[V9X_MARKER_BYTES];
    unsigned char same;
    unsigned index;

    for (index = 0u; index < V9X_MARKER_BYTES; ++index) {
        through_far[index] = rom[index];
    }
    read_flat_bytes(V9X_SELFTEST_LINEAR, flat_bytes, V9X_MARKER_BYTES);

    wr_x32("SelfTestLinear", V9X_SELFTEST_LINEAR);
    wr_bytes("SelfTestFarPointer", through_far, V9X_MARKER_BYTES);
    wr_bytes("SelfTestFlatSegment", flat_bytes, V9X_MARKER_BYTES);

    if (memcmp(through_far, flat_bytes, V9X_MARKER_BYTES) != 0) {
        wr_str("SelfTestStatus", "failed-readings-disagree");
        return 0;
    }
    if (all_same(through_far, V9X_MARKER_BYTES, &same)) {
        /* Both agree on a constant, which proves nothing - a broken flat read
         * returning FFh would match a ROM that happened to read FFh. */
        wr_str("SelfTestStatus", "inconclusive-selftest-target-is-uniform");
        return 0;
    }
    wr_str("SelfTestStatus", "ok");
    return 1;
}

/* ------------------------------------------------------------------ */
/* S3 registers                                                        */
/* ------------------------------------------------------------------ */

static unsigned crtc_index_port(void)
{
    return (inp(0x03ccu) & 0x01u) != 0u ? 0x03d4u : 0x03b4u;
}

static unsigned char read_indexed(unsigned index_port, unsigned char index)
{
    outp(index_port, index);
    return (unsigned char)inp(index_port + 1u);
}

static void write_indexed(unsigned index_port, unsigned char index,
                          unsigned char value)
{
    outp(index_port, index);
    outp(index_port + 1u, value);
}

/*
 * The documented CR38/CR39 keys, the same pair the display driver and the
 * survey's Tier 2 both write. On the 486 VLB Trio64 these registers do not read
 * back what is written to them - measured 2026-08-21 - so what is saved here is
 * whatever they report rather than their true contents, and putting it back is a
 * gesture at restoration rather than a real one. It is still the right gesture:
 * the observed end state after the survey did exactly this, three times, was
 * byte-identical to the state before.
 */
static void s3_unlock(unsigned index_port)
{
    saved_lock1 = read_indexed(index_port, 0x38u);
    saved_lock2 = read_indexed(index_port, 0x39u);
    write_indexed(index_port, 0x38u, 0x48u);
    write_indexed(index_port, 0x39u, 0xa5u);
}

static void s3_relock(unsigned index_port)
{
    write_indexed(index_port, 0x39u, saved_lock2);
    write_indexed(index_port, 0x38u, saved_lock1);
}

/*
 * Is the extended bank actually readable?
 *
 * Measured 2026-08-21, the hard way. When the bank is locked on this card every
 * register in it reads back the same constant - 42h on the run that found it -
 * so CR58, CR59 and CR5A all agreed and the base they described was 42420000h,
 * an address that exists nowhere. Three probes were spent on it before the
 * pattern was obvious.
 *
 * Three registers reading identically is not proof of a lock, but on a card
 * whose window is at 7F000000h with a size code of 3 it is close enough to
 * refuse to proceed on. The cost of a false alarm is a re-run; the cost of
 * missing it is a report full of confident readings of nothing.
 */
static int extended_bank_readable(unsigned index_port)
{
    unsigned char cr58 = read_indexed(index_port, 0x58u);
    unsigned char cr59 = read_indexed(index_port, 0x59u);
    unsigned char cr5a = read_indexed(index_port, 0x5au);

    return !(cr58 == cr59 && cr59 == cr5a);
}

/* ------------------------------------------------------------------ */
/* A20                                                                 */
/* ------------------------------------------------------------------ */

/*
 * Strictly speaking this is belt and braces. A20 masks physical address bit 20,
 * and neither candidate base has that bit set, nor do the 32 bytes read from it.
 * But "strictly speaking" is doing a lot of work in that sentence, and the BIOS
 * offers the enable as a service, so the doubt is removed for the price of two
 * INT 15h calls and restored afterwards.
 */
static int a20_query(void)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    input.x.ax = 0x2402u;
    int86(0x15, &input, &output);
    if (output.x.cflag != 0 || output.h.ah != 0u) return -1;
    return output.h.al != 0u ? 1 : 0;
}

static int a20_set(int enable)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    input.x.ax = enable ? 0x2401u : 0x2400u;
    int86(0x15, &input, &output);
    return (output.x.cflag == 0 && output.h.ah == 0u) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The marker in video memory                                          */
/* ------------------------------------------------------------------ */

static unsigned short vbe_call(unsigned short function, unsigned short bx,
                               unsigned short dx)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    input.x.ax = function;
    input.x.bx = bx;
    input.x.dx = dx;
    int86(0x10, &input, &output);
    return output.x.ax;
}

static unsigned char mode_info[256];

/*
 * 4F01h for one mode. The field that matters is PhysBasePtr at offset 40: with
 * an S3VBE TSR resident this is the address it has chosen for the linear
 * framebuffer, which is directly comparable with the CR59/CR5A the card itself
 * holds - and on the machine measured 2026-08-21 the two disagreed.
 */
static int vbe_mode_info(unsigned short mode)
{
    union REGS input;
    union REGS output;
    struct SREGS segments;

    memset(mode_info, 0, sizeof(mode_info));
    memset(&input, 0, sizeof(input));
    segread(&segments);
    input.x.ax = 0x4f01u;
    input.x.cx = mode;
    segments.es = FP_SEG((void far *)mode_info);
    input.x.di = FP_OFF((void far *)mode_info);
    int86x(0x10, &input, &output, &segments);
    return output.x.ax == 0x004fu;
}

static unsigned long mode_info_u32(unsigned at)
{
    return (unsigned long)mode_info[at] |
           ((unsigned long)mode_info[at + 1u] << 8) |
           ((unsigned long)mode_info[at + 2u] << 16) |
           ((unsigned long)mode_info[at + 3u] << 24);
}

static void restore_text_mode(void)
{
    union REGS input;
    union REGS output;

    if (!mode_changed) return;
    memset(&input, 0, sizeof(input));
    input.h.ah = 0x00u;
    input.h.al = 0x03u;
    int86(0x10, &input, &output);
    mode_changed = 0;
}

/*
 * Put a recognisable marker at offset 0 of video memory, through the path that
 * is already known to work.
 *
 * A0000h is the banked window every VBE 1.2 application draws through, so if
 * the marker reads back correctly there, video memory has genuinely been
 * written. That is what makes a later match at the linear base mean something:
 * not "the address returned data" but "the address returned *this* data", which
 * no coincidence and no floating bus supplies.
 *
 * The marker is written before linear addressing is touched, because on some S3
 * parts enabling it takes the banked window away.
 */
static unsigned long vbe_reported_base;

static int write_marker(void)
{
    unsigned char far *window = (unsigned char far *)MK_FP(0xa000u, 0u);
    unsigned short mode = 0x0101u;
    unsigned short status;
    unsigned index;

    if (want_linear) mode |= 0x4000u;

    for (index = 0u; index < V9X_MARKER_BYTES; ++index) {
        marker[index] = (unsigned char)(0x5au ^ (index * 7u + 0x13u));
    }

    wr_section("Pattern");
    wr_x16("ModeRequested", mode);
    wr_str("Method", want_linear ? "linear-mode-marker-through-the-flat-segment"
                                 : "banked-mode-marker-through-the-a0000-window");
    wr_bytes("Marker", marker, V9X_MARKER_BYTES);

    status = vbe_call(0x4f02u, mode, 0u);
    wr_x16("ModeSetStatus", status);
    if (status != 0x004fu) {
        wr_status("failed");
        wr_str("Reason", want_linear ? "vbe-4f02-refused-a-linear-mode"
                                     : "vbe-4f02-refused-mode-101h");
        return 0;
    }
    mode_changed = 1;

    /* What the VBE provider - the card's ROM, or a TSR standing in front of it -
     * says about this mode now that it is set. */
    if (vbe_mode_info(mode & 0x3fffu)) {
        vbe_reported_base = mode_info_u32(40u);
        wr_x16("ModeAttributes", (unsigned short)mode_info[0] |
               ((unsigned short)mode_info[1] << 8));
        wr_x32("VbeReportedPhysBase", vbe_reported_base);
    } else {
        wr_str("ModeInfoStatus", "unavailable");
    }

    if (want_linear) {
        /*
         * A linear mode generally takes the A0000h window away, so there is no
         * known-good path to write through. The marker goes in through the flat
         * segment instead, in probe_address, where the top-of-RAM guard lives.
         */
        wr_status("deferred-to-flat-write");
        return 1;
    }

    status = vbe_call(0x4f05u, 0x0000u, 0x0000u);
    wr_x16("WindowSetStatus", status);

    for (index = 0u; index < V9X_MARKER_BYTES; ++index) {
        window[index] = marker[index];
    }
    for (index = 0u; index < V9X_MARKER_BYTES; ++index) {
        readback[index] = window[index];
    }
    window_written = 1;

    wr_bytes("ReadBackThroughWindow", readback, V9X_MARKER_BYTES);
    if (memcmp(marker, readback, V9X_MARKER_BYTES) != 0) {
        wr_status("failed");
        wr_str("Reason", "marker-did-not-survive-the-banked-window");
        window_written = 0;
        return 0;
    }
    wr_status("ok");
    return 1;
}


/* ------------------------------------------------------------------ */
/* The probe                                                           */
/* ------------------------------------------------------------------ */

static void probe_address(unsigned index, const char *label,
                          unsigned long linear)
{
    char section[32];
    unsigned char same;

    sprintf(section, "Probe.%u", index);
    wr_section(section);
    wr_str("Label", label);
    wr_x32("Linear", linear);

    read_flat_bytes(linear, flat_bytes, V9X_MARKER_BYTES);
    wr_bytes("DataBefore", flat_bytes, V9X_MARKER_BYTES);
    if (all_same(flat_bytes, V9X_MARKER_BYTES, &same)) {
        wr_str("UniformBefore", "yes");
        wr_x8("UniformValueBefore", same);
    } else {
        wr_str("UniformBefore", "no");
    }

    /*
     * The banked cross-check, when there was a banked window to use. A match
     * here is the strongest result this tool can produce: the marker went in
     * through a path known to work and came out at the address under test, so
     * the two are the same memory. Nothing about a floating bus or a coincidence
     * produces thirty-two specific bytes.
     */
    if (window_written) {
        if (memcmp(marker, flat_bytes, V9X_MARKER_BYTES) == 0) {
            wr_status("marker-found");
            wr_str("Verdict", "live - the aperture is the same memory as the "
                              "banked window");
            return;
        }
        wr_status("marker-absent");
        wr_str("Verdict", "the marker is not visible here; suggestive of "
                          "nothing decoding, but not conclusive");
        return;
    }

    /*
     * No banked window, so write and read back through the flat segment. Only
     * above the top of installed RAM: below it a successful round trip would
     * prove nothing except that RAM works, and would have overwritten something.
     */
    if (want_write) {
        if (top_of_ram == 0ul) {
            wr_status("write-refused");
            wr_str("Reason", "top-of-ram-unknown");
            return;
        }
        if (linear < top_of_ram) {
            wr_status("write-refused");
            wr_str("Reason", "target-is-at-or-below-the-top-of-installed-ram");
            wr_x32("TopOfRam", top_of_ram);
            return;
        }
        write_flat_bytes(linear, marker, V9X_MARKER_BYTES);
        read_flat_bytes(linear, flat_bytes, V9X_MARKER_BYTES);
        wr_bytes("DataAfterWrite", flat_bytes, V9X_MARKER_BYTES);
        if (memcmp(marker, flat_bytes, V9X_MARKER_BYTES) == 0) {
            wr_status("round-trip-ok");
            wr_str("Verdict", "live - a marker written here read back "
                              "unchanged, and this is above installed RAM, so "
                              "it is video memory or nothing");
            return;
        }
        wr_status("round-trip-failed");
        wr_str("Verdict", "dead - a marker written here did not read back");
        return;
    }

    wr_status("read-only");
    wr_str("Verdict", "no marker was placed, so these bytes identify nothing - "
                      "add /pattern, or /pattern /linear");
}


static void report_window_registers(unsigned index_port, const char *when)
{
    unsigned char cr58 = read_indexed(index_port, 0x58u);
    unsigned char cr59 = read_indexed(index_port, 0x59u);
    unsigned char cr5a = read_indexed(index_port, 0x5au);
    char key[32];

    sprintf(key, "CR58%s", when);
    wr_x8(key, cr58);
    sprintf(key, "CR59%s", when);
    wr_x8(key, cr59);
    sprintf(key, "CR5A%s", when);
    wr_x8(key, cr5a);
    sprintf(key, "Base%s", when);
    wr_x32(key, ((unsigned long)cr59 << 24) | ((unsigned long)cr5a << 16));
}

/* ------------------------------------------------------------------ */
/* Startup                                                            */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    puts("Velocity9x VLB linear-aperture probe (" V9X_BUILD_ID ")");
    puts("");
    puts("  V9XAPER                    read the window's address, no writes");
    puts("                             to it and no mode set");
    puts("  V9XAPER /pattern           set a mode and put a marker in video");
    puts("                             memory first - needed for any positive");
    puts("                             answer");
    puts("  V9XAPER /pattern /linear   ask for a LINEAR framebuffer mode, so a");
    puts("                             loaded S3VBE TSR places the window and");
    puts("                             this reports where; implies /write");
    puts("  V9XAPER /write             place the marker through the flat");
    puts("                             segment instead of the A0000h window;");
    puts("                             refused at or below the top of RAM");
    puts("  V9XAPER /enable            turn linear addressing on in CR58[4]");
    puts("  V9XAPER /relocate:HHHHHHHH move the window to this base first");
    puts("  V9XAPER /base:HHHHHHHH     also probe this address");
    puts("  V9XAPER /out:PATH          write the report somewhere else");
    puts("");
    puts("NOT for testers. This writes to the card and sets a video mode.");
    puts("Run it on hardware you own and can power-cycle. A clean boot is");
    puts("required: press F5 at 'Starting MS-DOS' so no memory manager is");
    puts("resident - unreal mode cannot be entered from virtual-8086 mode.");
}

static unsigned long parse_hex(const char *text)
{
    unsigned long value = 0ul;

    while (*text != '\0') {
        char digit = *text++;

        if (digit >= '0' && digit <= '9') {
            value = (value << 4) | (unsigned long)(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            value = (value << 4) | (unsigned long)(digit - 'a' + 10);
        } else if (digit >= 'A' && digit <= 'F') {
            value = (value << 4) | (unsigned long)(digit - 'A' + 10);
        } else {
            break;
        }
    }
    return value;
}

static void restore_break_state(void)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    input.x.ax = 0x3301u;
    input.h.dl = saved_break_state;
    int86(0x21, &input, &output);
}

/*
 * The single exit path. Everything this tool changed is put back here, in the
 * reverse of the order it was changed, and it runs on every route out including
 * the refusals - which is why the flags guarding each step exist.
 */
static void restore_everything(unsigned index_port)
{
    if (mode_changed) restore_text_mode();
    /*
     * Restoring the window registers has to happen after the text mode is back,
     * because a BIOS mode set writes the extended CRTC bank itself and would
     * otherwise undo this - and it has to re-open the lock first, because that
     * same mode set may well have closed it. Writing CR58 through a closed lock
     * would fail silently, which is the worst of the available outcomes.
     *
     * Guarded on window_saved: a refusal before the registers were read would
     * otherwise write three zeroes into them, which is worse than anything this
     * tool was asked to do.
     */
    if (window_saved) {
        write_indexed(index_port, 0x38u, 0x48u);
        write_indexed(index_port, 0x39u, 0xa5u);
        if (want_enable || want_relocate) {
            write_indexed(index_port, 0x5au, saved_cr5a);
            write_indexed(index_port, 0x59u, saved_cr59);
            write_indexed(index_port, 0x58u, saved_cr58);
        }
        s3_relock(index_port);
    }
    if (unreal_active) {
        leave_unreal();
        unreal_active = 0;
    }
}

int main(int argc, char **argv)
{
    int index;
    unsigned index_port;
    const char *requested_path = V9X_PROBE_DEFAULT;
    int a20_before = -1;
    int probes = 0;
    unsigned long card_base;

    for (index = 1; index < argc; ++index) {
        const char *option = argv[index];

        if (option[0] != '/' && option[0] != '-') continue;
        ++option;
        if (stricmp(option, "pattern") == 0) {
            want_pattern = 1;
        } else if (stricmp(option, "linear") == 0) {
            want_linear = 1;
        } else if (stricmp(option, "write") == 0) {
            want_write = 1;
        } else if (stricmp(option, "enable") == 0) {
            want_enable = 1;
        } else if (strnicmp(option, "relocate:", 9) == 0) {
            want_relocate = 1;
            relocate_base = parse_hex(option + 9);
        } else if (strnicmp(option, "base:", 5) == 0) {
            have_extra_base = 1;
            extra_base = parse_hex(option + 5);
        } else if (strnicmp(option, "out:", 4) == 0) {
            requested_path = option + 4;
        } else {
            print_usage();
            return 0;
        }
    }

    /* A linear mode has no banked window to write through, so the flat write is
     * the only way to place a marker at all. Asking for one is asking for both. */
    if (want_linear) want_write = 1;

    strncpy(report_path, requested_path, sizeof(report_path) - 1u);
    report_path[sizeof(report_path) - 1u] = '\0';
    report = fopen(report_path, "wt");
    if (report == 0) {
        puts("Velocity9x aperture probe: could not create a report file.");
        return 3;
    }

    {
        union REGS input;
        union REGS output;
        char text[64];

        memset(&input, 0, sizeof(input));
        input.x.ax = 0x3300u;
        int86(0x21, &input, &output);
        saved_break_state = output.h.dl;
        memset(&input, 0, sizeof(input));
        input.x.ax = 0x3301u;
        input.h.dl = 0x00u;
        int86(0x21, &input, &output);

        fprintf(report, "; Velocity9x VLB linear-aperture probe\n");
        wr_section("Report");
        wr_str("SchemaVersion", V9X_PROBE_SCHEMA);
        wr_str("Tool", "V9XAPER");
        wr_str("Build", V9X_BUILD_ID);

        memset(&input, 0, sizeof(input));
        input.h.ah = 0x2au;
        int86(0x21, &input, &output);
        sprintf(text, "%04u-%02u-%02u", output.x.cx, output.h.dh, output.h.dl);
        wr_str("Date", text);
        memset(&input, 0, sizeof(input));
        input.h.ah = 0x2cu;
        int86(0x21, &input, &output);
        sprintf(text, "%02u:%02u:%02u", output.h.ch, output.h.cl, output.h.dh);
        wr_str("Time", text);

        text[0] = '\0';
        for (index = 1; index < argc; ++index) {
            if (strlen(text) + strlen(argv[index]) + 2u >= sizeof(text)) break;
            if (text[0] != '\0') strcat(text, " ");
            strcat(text, argv[index]);
        }
        wr_str("CommandLine", text);
        wr_str("Stage", want_relocate ? "relocate"
                                      : (want_enable ? "enable"
                                                     : (want_pattern ? "pattern"
                                                                     : "read-only")));

        memset(&input, 0, sizeof(input));
        input.x.ax = 0x1600u;
        int86(0x2f, &input, &output);
        windows_present = !(output.h.al == 0x00u || output.h.al == 0x80u);
    }

    puts("Velocity9x VLB linear-aperture probe (" V9X_BUILD_ID ")");

    /*
     * The gate. Unreal mode needs a 386 and needs the machine not already to be
     * in protected or virtual-8086 mode; from inside a V86 monitor the LGDT and
     * the CR0 write fault instead of working. Refusing is the only correct
     * response - there is no reduced-capability version of this probe.
     */
    wr_section("Gate");
    if ((flags_probe() & 0xf000u) == 0xf000u) {
        wr_status("refused");
        wr_str("Reason", "cpu-is-8086-or-186");
        puts("This machine has no 386, so unreal mode is not available.");
        goto finish;
    }
    if ((flags_set_probe() & 0x7000u) == 0x0000u) {
        wr_status("refused");
        wr_str("Reason", "cpu-386-not-confirmed");
        puts("Could not confirm a 386. Refusing to execute 32-bit opcodes.");
        goto finish;
    }
    cpu_386 = 1;
    {
        unsigned short msw = machine_status_word();

        in_v86 = (msw & 0x0001u) != 0u;
        wr_x16("MachineStatusWord", msw);
    }
    wr_str("ProtectedOrV86", in_v86 ? "yes" : "no");
    wr_str("WindowsPresent", windows_present ? "yes" : "no");

    if (in_v86 || windows_present) {
        wr_status("refused");
        wr_str("Reason", in_v86 ? "cpu-in-protected-or-v86-mode"
                                : "windows-is-running");
        puts("");
        puts("A memory manager or Windows owns the CPU's mode right now, so");
        puts("unreal mode cannot be entered. Reboot and press F5 at");
        puts("'Starting MS-DOS' to skip CONFIG.SYS, then run this again.");
        goto finish;
    }
    wr_status("ok");

    index_port = crtc_index_port();

    /*
     * Installed RAM, for one purpose: /write must not put a marker anywhere the
     * machine might already be using. AH=88h is enough here because this probe
     * refuses to run under a memory manager in the first place, so nothing is
     * intercepting it.
     */
    {
        union REGS input;
        union REGS output;

        memset(&input, 0, sizeof(input));
        input.h.ah = 0x88u;
        int86(0x15, &input, &output);
        if (output.x.cflag == 0 && output.x.ax != 0u) {
            top_of_ram = 0x100000ul + ((unsigned long)output.x.ax * 1024ul);
        }
        wr_section("Memory");
        wr_x32("TopOfRam", top_of_ram);
        wr_str("TopOfRamSource",
               top_of_ram != 0ul ? "int15h-ah88h" : "unknown");
    }

    /*
     * The 486 measured on 2026-08-21 has no INT 15h AX=2402h, so this reports
     * unknown and nothing is changed. That is the right outcome rather than a
     * gap: A20 masks physical address bit 20, and no address this probe reads
     * has that bit set, so the state is recorded for the record and not acted
     * on unless the BIOS both answers and says it is off.
     */
    wr_section("A20");
    a20_before = a20_query();
    wr_str("Query", a20_before < 0 ? "unsupported-by-this-bios" : "ok");
    if (a20_before >= 0) {
        wr_str("Before", a20_before ? "enabled" : "disabled");
    }
    if (a20_before == 0) {
        wr_str("EnableAttempted", a20_set(1) ? "ok" : "failed");
        wr_str("After", a20_query() > 0 ? "enabled" : "disabled");
    }
    wr_str("MattersHere", "no-probed-address-has-bit-20-set");

    wr_section("UnrealMode");
    build_gdt();
    enter_unreal(gdtr);
    unreal_active = 1;
    if (!unreal_self_test()) {
        wr_status("refused");
        wr_str("Reason", "self-test-did-not-prove-the-flat-read");
        puts("");
        puts("Unreal mode did not prove itself against the BIOS ROM, so any");
        puts("reading it produced would be worthless. Nothing else was tried.");
        goto finish;
    }
    wr_status("ok");
    puts("Unreal mode verified against the system BIOS ROM.");

    /* The card's own idea of where its window is. CR58/CR59/CR5A sit behind the
     * extended-register lock, so this is the one write the read-only stage
     * makes - the same documented pair the survey's Tier 2 writes. */
    wr_section("Chipset.S3");
    s3_unlock(index_port);
    wr_x8("LockedCR38", saved_lock1);
    wr_x8("LockedCR39", saved_lock2);
    saved_cr58 = read_indexed(index_port, 0x58u);
    saved_cr59 = read_indexed(index_port, 0x59u);
    saved_cr5a = read_indexed(index_port, 0x5au);
    window_saved = 1;
    report_window_registers(index_port, "AsFound");
    card_base = ((unsigned long)saved_cr59 << 24) |
                ((unsigned long)saved_cr5a << 16);
    wr_str("LinearAddressingAsFound",
           (saved_cr58 & 0x10u) != 0u ? "enabled" : "disabled");

    if (want_pattern && !write_marker()) {
        puts("The marker could not be placed in video memory; stopping.");
        goto finish;
    }

    /*
     * A BIOS mode set closes the extended register lock behind itself.
     *
     * This card's own ROM does; an S3VBE TSR standing in front of it does not,
     * and leaves CR38 reading 48h afterwards. That difference cost three runs:
     * every window register read after the ROM's mode set came back 42h, the
     * probe assembled a base of 42420000h out of them, and the CR58 write that
     * was supposed to enable linear addressing went through a closed lock and
     * did nothing - which the report then blamed on the card not honouring a
     * read-back.
     *
     * So the lock is re-opened here, after the mode set and before anything
     * touches the window, and the state either side of it is recorded rather
     * than assumed. The driver already unlocks before every extended access for
     * the same reason; nothing about that needs to change.
     */
    if (want_pattern) {
        wr_section("Chipset.S3.AfterModeSet");
        wr_x8("CR38", read_indexed(index_port, 0x38u));
        wr_x8("CR39", read_indexed(index_port, 0x39u));
        wr_str("BankReadableBeforeReunlock",
               extended_bank_readable(index_port) ? "yes" : "no");
        write_indexed(index_port, 0x38u, 0x48u);
        write_indexed(index_port, 0x39u, 0xa5u);
        wr_str("BankReadableAfterReunlock",
               extended_bank_readable(index_port) ? "yes" : "no");
        report_window_registers(index_port, "AfterModeSet");
        if (!extended_bank_readable(index_port)) {
            wr_status("failed");
            wr_str("Reason", "extended-bank-still-unreadable-after-reunlock");
            puts("");
            puts("The card's extended registers will not read back after the");
            puts("mode set, so the window's address cannot be trusted and");
            puts("nothing was probed. This is a tool problem, not a result.");
            goto finish;
        }
        wr_status("ok");
    }

    /* Committed to disk before anything touches the window registers. */
    fflush(report);

    if (want_relocate) {
        write_indexed(index_port, 0x59u,
                      (unsigned char)((relocate_base >> 24) & 0xfful));
        write_indexed(index_port, 0x5au,
                      (unsigned char)((relocate_base >> 16) & 0xfful));
    }
    if (want_enable) {
        /* Size code 11b for a 4 MiB window plus CR58[4], exactly what
         * v9x_s3_enable_linear_aperture writes - so this also measures whether
         * that function's read-back guard can ever pass on this card. */
        write_indexed(index_port, 0x58u,
                      (unsigned char)((saved_cr58 & 0xfcu) | 0x13u));
    }
    if (want_relocate || want_enable) {
        wr_section("Chipset.S3.After");
        report_window_registers(index_port, "AfterWrite");
        /*
         * Only a meaningful answer when the bank reads back at all. Reported as
         * a lock problem otherwise, because a locked bank returning a constant
         * would otherwise be recorded as the card refusing the write - and that
         * is a claim about the hardware which would be false.
         */
        if (extended_bank_readable(index_port)) {
            wr_str("Cr58ReadBackHonoured",
                   (read_indexed(index_port, 0x58u) & 0x13u) == 0x13u
                       ? "yes" : "no");
        } else {
            wr_str("Cr58ReadBackHonoured", "unknown-bank-not-readable");
        }
        fflush(report);
    }

    /*
     * Probe the base the registers hold, not the one that was asked for. CR59
     * and CR5A carry bits 31:16 only, so a /relocate value with low bits set is
     * silently truncated - and on a card where CR38/CR39 do not read back what
     * is written, assuming any write landed is exactly the mistake to avoid.
     * Reading it back costs two port accesses and removes the assumption.
     */
    {
        unsigned char cr59 = read_indexed(index_port, 0x59u);
        unsigned char cr5a = read_indexed(index_port, 0x5au);
        unsigned long effective = ((unsigned long)cr59 << 24) |
                                  ((unsigned long)cr5a << 16);

        if (want_relocate && effective != relocate_base) {
            wr_section("Relocate");
            wr_status("partial");
            wr_x32("Requested", relocate_base);
            wr_x32("Effective", effective);
            wr_str("Reason",
                   "cr59-cr5a-hold-bits-31-16-only-or-the-write-did-not-land");
        }
        probe_address(probes++, "card-window-base", effective);
    }
    if (have_extra_base) {
        probe_address(probes++, "operator-supplied", extra_base);
    }
    /*
     * If the VBE provider named a base of its own and it is not the one the card
     * holds, probe that too. This is the case worth catching: with S3VBE
     * resident the two disagreed, and the provider's choice is the one made by
     * software that actually drives linear modes on these boards.
     */
    if (vbe_reported_base != 0ul &&
        vbe_reported_base != (want_relocate ? relocate_base : card_base) &&
        (!have_extra_base || vbe_reported_base != extra_base)) {
        probe_address(probes++, "vbe-reported-physbase", vbe_reported_base);
    }

finish:
    if (cpu_386) restore_everything(crtc_index_port());
    if (a20_before == 0) a20_set(0);

    wr_section("Result");
    /* Written after restore_everything has run, so it describes what happened
     * rather than what was intended. */
    wr_str("Restored", window_saved ? "yes" : "nothing-to-restore");
    wr_str("Complete", "yes");
    fclose(report);
    restore_break_state();

    printf("Report written to %s\n", report_path);
    return 0;
}
