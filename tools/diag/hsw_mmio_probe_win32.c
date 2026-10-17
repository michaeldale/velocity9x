/*
 * V9XHSWMM.EXE: Haswell (Gen7.5) read-only MMIO and GTT snapshot.
 *
 * Phase 2 of docs\plans\intel-haswell-bringup.md. Reads GTTMMADR from the
 * IGD's config space, loads HSWMM.VXD, and asks it for a fixed list of
 * register offsets (each read twice) and the head of the GTT. Writes
 * C:\V9XDIAG\V9XHSWMM.INI:
 *
 *   [Report]  build, status, base, valid GTT entries in the whole table;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> where the PRM gives the register a name;
 *   [Gtt]     the head of the table decoded with v9x_intel_hsw_decode_pte:
 *             the first entries raw, the run of valid entries from 0 and
 *             whether it maps contiguous stolen memory from BDSM.
 *
 * Without arguments nothing on the device is written; see
 * tools\diag\hsw_mmio_probe.asm for the windows the VxD accepts. The only
 * config access here is mechanism #1 reads, as in V9XHSW.EXE.
 *
 * /forcewake is Phase A of docs\plans\intel-haswell-direct3d.md: the VxD
 * requests forcewake, takes the second snapshot while it is held, and
 * releases it (two writes to A188h; the sequence is in the VxD's header).
 * Each R<offset> is then asleep[,awake if it moved], and [Forcewake] records
 * the handshake.
 *
 * Names are the ShortNames of HSW PRM Vol 2c where its register list gives
 * an address. A name marked "(i915)" is not in the public PRM's register
 * list - the PRM refers to it only by name - and the address comes from the
 * Linux i915 driver; treat its reading accordingly.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/intel_hsw_3d.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_HSWMM_PATH       V9X_DIAG_PATH("V9XHSWMM.INI")
#define V9X_HSWMM_VXD        "\\\\.\\HSWMM.VXD"
#define V9X_HSWMM_MAGIC      0x4d4d5348ul
#define V9X_HSWMM_CAPTURE    1u
#define V9X_HSWMM_OFFSET_MAX 256u
#define V9X_HSWMM_GTT_COPY   4096u
#define V9X_HSWMM_HEADER     16u
#define V9X_HSWMM_FORCEWAKE  0x00000001ul
#define V9X_HSWMM_GTT_RAW    16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_IGD_CONFIG       0x80001000ul

/* Status bits, as hsw_mmio_probe.asm defines them. */
#define V9X_HSWMM_ST_IGD_INTEL       0x00000001ul
#define V9X_HSWMM_ST_BASE_INVALID    0x00000002ul
#define V9X_HSWMM_ST_MMIO_MAPPED     0x00000004ul
#define V9X_HSWMM_ST_GTT_MAPPED      0x00000008ul
#define V9X_HSWMM_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_HSWMM_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_HSWMM_ST_GTT_READ        0x00000040ul
#define V9X_HSWMM_ST_FW_ALREADY_HELD 0x00000100ul
#define V9X_HSWMM_ST_FW_REQUESTED    0x00000200ul
#define V9X_HSWMM_ST_FW_ACKED        0x00000400ul
#define V9X_HSWMM_ST_FW_ACK_TIMEOUT  0x00000800ul
#define V9X_HSWMM_ST_FW_RELEASED     0x00001000ul
#define V9X_HSWMM_ST_FW_RELEASE_TIMEOUT 0x00002000ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_hswmm_register {
    DWORD offset;
    const char *name;
};

/*
 * Whole blocks where the question is "what is there": the render command
 * streamer's first 256 bytes (ring, ACTHD, error and mode registers), and
 * the four blitter ring registers. Single registers elsewhere.
 */
static const struct v9x_hswmm_register v9x_named[] = {
    { 0x02030ul, "RCS_RING_BUFFER_TAIL" },
    { 0x02034ul, "RCS_RING_BUFFER_HEAD" },
    { 0x02038ul, "RCS_RING_BUFFER_START" },
    { 0x0203cul, "RCS_RING_BUFFER_CTL" },
    { 0x02074ul, "ACTHD (i915; intel_gma.h: gen4+)" },
    { 0x020b0ul, "EIR (i915)" },
    { 0x020b4ul, "EMR (i915)" },
    { 0x020b8ul, "ESR (i915)" },
    { 0x22030ul, "BCS_RING_BUFFER_TAIL" },
    { 0x22034ul, "BCS_RING_BUFFER_HEAD" },
    { 0x22038ul, "BCS_RING_BUFFER_START" },
    { 0x2203cul, "BCS_RING_BUFFER_CTL" },
    { 0x0a090ul, "RC_CONTROL (i915)" },
    { 0x0a094ul, "RC_STATE (i915)" },
    { 0x0a188ul, "MULTIFORCEWAKE (named in Vol 12)" },
    { 0x130040ul, "LCPLL_CTL" },
    { 0x130044ul, "forcewake acknowledge (Vol 12)" },
    { 0x41000ul, "VGA_CONTROL" },
    { 0x44050ul, "DE_RRMR" },
    { 0x45400ul, "PWR_WELL_CTL1" },
    { 0x45404ul, "PWR_WELL_CTL2" },
    { 0x45408ul, "PWR_WELL_CTL3 (i915)" },
    { 0x4540cul, "PWR_WELL_CTL4 (i915)" },
    { 0x60000ul, "PIPE_HTOTAL_A" },
    { 0x60004ul, "PIPE_HBLANK_A" },
    { 0x60008ul, "PIPE_HSYNC_A" },
    { 0x6000cul, "PIPE_VTOTAL_A" },
    { 0x6001cul, "PIPE_SRCSZ_A" },
    { 0x70008ul, "PIPE_CONF_A" },
    { 0x70180ul, "PRI_CTL_A" },
    { 0x70188ul, "PRI_STRIDE_A" },
    { 0x7019cul, "PRI_SURF_A" },
    { 0x701a4ul, "PRI_OFFSET_A" },
    { 0x701acul, "PRI_SURFLIVE_A" },
    { 0xe0000ul, "TRANS_HTOTAL_A" },
    { 0xe0004ul, "TRANS_HBLANK_A" },
    { 0xe0008ul, "TRANS_HSYNC_A" },
    { 0xe000cul, "TRANS_VTOTAL_A" },
    { 0xe1100ul, "DAC_CTL" }
};

#define V9X_RCS_BLOCK_BASE  0x02000ul
#define V9X_RCS_BLOCK_BYTES 0x100ul

struct v9x_hswmm_request {
    DWORD base;
    DWORD flags;
    DWORD count;
    DWORD offsets[V9X_HSWMM_OFFSET_MAX];
};

struct v9x_hswmm_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD accepted;
    DWORD gtt_valid;
    DWORD cf8;
    DWORD refused_offset;
    DWORD flags;
    DWORD fw_ack_before;
    DWORD fw_polls_to_ack;
    DWORD fw_ack_awake;
    DWORD fw_request_awake;
    DWORD fw_polls_to_release;
    DWORD fw_ack_after;
    DWORD reserved;
    DWORD offsets[V9X_HSWMM_OFFSET_MAX];
    DWORD first[V9X_HSWMM_OFFSET_MAX];
    DWORD second[V9X_HSWMM_OFFSET_MAX];
    DWORD gtt[V9X_HSWMM_GTT_COPY];
};

/* The header must be the VxD's HSW_HEADER_DWORDS long. */
typedef char v9x_hswmm_header_matches[
    offsetof(struct v9x_hswmm_result, offsets) ==
    sizeof(DWORD) * V9X_HSWMM_HEADER ? 1 : -1];

static struct v9x_hswmm_request v9x_request;
static struct v9x_hswmm_result v9x_result;

/*
 * Phase B1 (/bcs): docs\plans\intel-haswell-direct3d.md. The ring, status
 * page and scratch at aperture offset 1800000h; the VxD runs the stages and
 * this side builds the commands (hsw_3d.c, host-tested) and judges the
 * scratch. Layouts match hsw_mmio_probe.asm's HSW_BCS_* definitions.
 *
 * Not 1F00000h, the first placement: the top megabyte of stolen memory
 * would not take a CPU write through the aperture (run 1, stage 2;
 * docs\decisions\2026-10-17-haswell-phase-b1-bcs-ring.md).
 */
#define V9X_BCS_DIOC         2u
#define V9X_BCS_MAGIC        0x43425348ul
#define V9X_BCS_OFFSET       0x01800000ul
#define V9X_BCS_STATUS       (V9X_BCS_OFFSET + 0x1000ul)
#define V9X_BCS_SCRATCH      (V9X_BCS_OFFSET + 0x2000ul)
#define V9X_BCS_SCRATCH_DW   0x4000ul
#define V9X_BCS_GUARD        0xa5a5a5a5ul
#define V9X_BCS_COLOR        0x11223344ul
#define V9X_BCS_PITCH_FIELD  256u
#define V9X_BCS_FILL         16u /* 16x16 pixels */
#define V9X_BCS_PAGES        18u

struct v9x_bcs_request {
    DWORD base;
    DWORD aperture;
    DWORD bdsm;
    DWORD offset;
    DWORD n1;
    DWORD n2;
    DWORD cmd1[8];
    DWORD cmd2[16];
};

struct v9x_bcs_result {
    DWORD magic;
    DWORD status;
    DWORD stage;
    DWORD ack_before;
    DWORD polls_ack;
    DWORD ack_after;
    DWORD polls_release;
    DWORD ctl_before;
    DWORD start_rb;
    DWORD ctl_rb;
    DWORD head_rb;
    DWORD tail_rb;
    DWORD polls1;
    DWORD head1;
    DWORD status0;
    DWORD polls2;
    DWORD head2;
    DWORD status1;
    DWORD acthd;
    DWORD eir;
    DWORD ctl_after;
    DWORD start_after;
    DWORD head_after;
    DWORD tail_after;
    DWORD cpu_readback;
    DWORD bad_pte;
    DWORD ptes[V9X_BCS_PAGES];
    DWORD reserved[64 - 26 - V9X_BCS_PAGES];
    DWORD scratch[V9X_BCS_SCRATCH_DW];
};

typedef char v9x_bcs_header_matches[
    offsetof(struct v9x_bcs_result, scratch) == 64u * sizeof(DWORD) &&
    offsetof(struct v9x_bcs_result, ptes) == 104u &&
    sizeof(struct v9x_bcs_request) == 120u ? 1 : -1];

static struct v9x_bcs_request v9x_bcs_in;
static struct v9x_bcs_result v9x_bcs_out;

static DWORD v9x_config_read(DWORD offset);
static void v9x_hex(char *text, DWORD value, int digits);

/* Does the scratch hold exactly the 16x16 fill at this byte stride, with
 * every other dword still the guard? */
static int v9x_bcs_stride_matches(DWORD stride)
{
    DWORD index;
    DWORD row;
    DWORD column;
    DWORD expected;

    for (index = 0ul; index < V9X_BCS_SCRATCH_DW; ++index) {
        row = (index * 4ul) / stride;
        column = ((index * 4ul) % stride) / 4ul;
        expected = (row < V9X_BCS_FILL && column < V9X_BCS_FILL) ?
                   V9X_BCS_COLOR : V9X_BCS_GUARD;
        if (v9x_bcs_out.scratch[index] != expected) {
            return 0;
        }
    }
    return 1;
}

static void v9x_bcs_key(const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA("Bcs", key, text, V9X_HSWMM_PATH);
}

static void v9x_bcs_publish(void)
{
    char key[16];
    DWORD index;
    DWORD filled;
    DWORD guards;
    const char *verdict;

    v9x_bcs_key("Status", v9x_bcs_out.status);
    v9x_bcs_key("Stage", v9x_bcs_out.stage);
    v9x_bcs_key("BadPte", v9x_bcs_out.bad_pte);
    v9x_bcs_key("CpuReadback", v9x_bcs_out.cpu_readback);
    v9x_bcs_key("AckBefore", v9x_bcs_out.ack_before);
    v9x_bcs_key("PollsToAck", v9x_bcs_out.polls_ack);
    v9x_bcs_key("CtlBefore", v9x_bcs_out.ctl_before);
    v9x_bcs_key("StartReadback", v9x_bcs_out.start_rb);
    v9x_bcs_key("CtlReadback", v9x_bcs_out.ctl_rb);
    v9x_bcs_key("HeadReadback", v9x_bcs_out.head_rb);
    v9x_bcs_key("TailReadback", v9x_bcs_out.tail_rb);
    v9x_bcs_key("Polls1", v9x_bcs_out.polls1);
    v9x_bcs_key("Head1", v9x_bcs_out.head1);
    v9x_bcs_key("Status0", v9x_bcs_out.status0);
    v9x_bcs_key("Polls2", v9x_bcs_out.polls2);
    v9x_bcs_key("Head2", v9x_bcs_out.head2);
    v9x_bcs_key("Status1", v9x_bcs_out.status1);
    v9x_bcs_key("Acthd", v9x_bcs_out.acthd);
    v9x_bcs_key("Eir", v9x_bcs_out.eir);
    v9x_bcs_key("CtlAfter", v9x_bcs_out.ctl_after);
    v9x_bcs_key("StartAfter", v9x_bcs_out.start_after);
    v9x_bcs_key("HeadAfter", v9x_bcs_out.head_after);
    v9x_bcs_key("TailAfter", v9x_bcs_out.tail_after);
    v9x_bcs_key("PollsToRelease", v9x_bcs_out.polls_release);
    v9x_bcs_key("AckAfter", v9x_bcs_out.ack_after);
    for (index = 0ul; index < V9X_BCS_PAGES; ++index) {
        lstrcpyA(key, "Pte");
        v9x_hex(key + 3, index, 2);
        v9x_bcs_key(key, v9x_bcs_out.ptes[index]);
    }

    filled = 0ul;
    guards = 0ul;
    for (index = 0ul; index < V9X_BCS_SCRATCH_DW; ++index) {
        if (v9x_bcs_out.scratch[index] == V9X_BCS_COLOR) {
            ++filled;
        } else if (v9x_bcs_out.scratch[index] == V9X_BCS_GUARD) {
            ++guards;
        }
    }
    v9x_bcs_key("ScratchColourDwords", filled);
    v9x_bcs_key("ScratchGuardDwords", guards);
    v9x_bcs_key("ScratchOtherDwords", V9X_BCS_SCRATCH_DW - filled - guards);
    for (index = 0ul; index < 8ul; ++index) {
        lstrcpyA(key, "Scratch");
        v9x_hex(key + 7, index, 2);
        v9x_bcs_key(key, v9x_bcs_out.scratch[index]);
    }

    if (v9x_bcs_stride_matches((DWORD)V9X_BCS_PITCH_FIELD)) {
        verdict = "bytes";
    } else if (v9x_bcs_stride_matches((DWORD)V9X_BCS_PITCH_FIELD * 4ul)) {
        verdict = "dwords";
    } else if (filled == 0ul) {
        verdict = "no-fill";
    } else {
        verdict = "neither";
    }
    WritePrivateProfileStringA("Bcs", "PitchUnit", verdict, V9X_HSWMM_PATH);
}

/* Build both command streams with the host-tested builders and check
 * them; returns 0 if either fails, which stops the run before the VxD. */
static int v9x_bcs_build(void)
{
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    DWORD probe[28];
    DWORD index;

    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)v9x_bcs_in.cmd1, 8ul);
    if (v9x_intel_hsw_mi_store_data_imm(&batch, V9X_BCS_STATUS,
                                        0xc0de0001ul) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_bcs_in.n1 = batch.used;

    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)v9x_bcs_in.cmd2, 16ul);
    if (v9x_intel_hsw_xy_color_blt(&batch, V9X_BCS_PITCH_FIELD, 0u, 0u,
                                   V9X_BCS_FILL, V9X_BCS_FILL,
                                   V9X_BCS_SCRATCH, V9X_BCS_COLOR) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_mi_store_data_imm(&batch, V9X_BCS_STATUS + 4ul,
                                        0xc0de0002ul) != V9X_STATUS_OK) {
        return 0;
    }
    while ((batch.used & 1ul) != 0ul) {
        v9x_intel_hsw_mi_noop(&batch);
    }
    v9x_bcs_in.n2 = batch.used;
    if (batch.overflow != 0u) {
        return 0;
    }

    /* The checker sees both streams as one batch with an end appended. */
    for (index = 0ul; index < v9x_bcs_in.n1; ++index) {
        probe[index] = v9x_bcs_in.cmd1[index];
    }
    for (index = 0ul; index < v9x_bcs_in.n2; ++index) {
        probe[v9x_bcs_in.n1 + index] = v9x_bcs_in.cmd2[index];
    }
    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)probe, 28ul);
    batch.used = v9x_bcs_in.n1 + v9x_bcs_in.n2;
    v9x_intel_hsw_mi_batch_buffer_end(&batch);
    v9x_intel_hsw_mi_noop(&batch);
    return v9x_intel_hsw_3d_check((v9x_u32 *)probe, batch.used, &check) ==
           V9X_STATUS_OK;
}

static void v9x_bcs_run(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    WritePrivateProfileStringA("Bcs", 0, 0, V9X_HSWMM_PATH);
    v9x_bcs_in.base = v9x_config_read(0x10ul) & V9X_INTEL_HSW_GTTMMADR_MASK;
    v9x_bcs_in.aperture = v9x_config_read(0x18ul) & 0xfffffff0ul;
    v9x_bcs_in.bdsm = v9x_config_read(V9X_INTEL_HSW_CFG_BDSM);
    v9x_bcs_in.offset = V9X_BCS_OFFSET;
    v9x_bcs_key("Aperture", v9x_bcs_in.aperture);
    v9x_bcs_key("Offset", v9x_bcs_in.offset);
    if (!v9x_bcs_build()) {
        WritePrivateProfileStringA("Bcs", "Result", "COMMANDS-REFUSED",
                                   V9X_HSWMM_PATH);
        return;
    }
    v9x_bcs_key("Commands1", v9x_bcs_in.n1);
    v9x_bcs_key("Commands2", v9x_bcs_in.n2);

    device = CreateFileA(V9X_HSWMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        WritePrivateProfileStringA("Bcs", "Result", "VXD-LOAD-FAILED",
                                   V9X_HSWMM_PATH);
        return;
    }
    ok = DeviceIoControl(device, V9X_BCS_DIOC, &v9x_bcs_in,
                         sizeof(v9x_bcs_in), &v9x_bcs_out,
                         sizeof(v9x_bcs_out), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_bcs_out) ||
        v9x_bcs_out.magic != V9X_BCS_MAGIC) {
        WritePrivateProfileStringA("Bcs", "Result", "DIOC-FAILED",
                                   V9X_HSWMM_PATH);
        return;
    }
    v9x_bcs_publish();
    WritePrivateProfileStringA("Bcs", "Result", "COMPLETE", V9X_HSWMM_PATH);
}

static DWORD v9x_config_read(DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(V9X_PCI_ADDRESS);
    v9x_outpd(V9X_PCI_ADDRESS, V9X_IGD_CONFIG | (offset & 0xfcul));
    value = v9x_inpd(V9X_PCI_DATA);
    v9x_outpd(V9X_PCI_ADDRESS, saved);
    return value;
}

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, V9X_HSWMM_PATH);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 6);
}

static void v9x_add(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_request.count; ++index) {
        if (v9x_request.offsets[index] == offset) {
            return;
        }
    }
    if (v9x_request.count < V9X_HSWMM_OFFSET_MAX) {
        v9x_request.offsets[v9x_request.count++] = offset;
    }
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;
    DWORD named;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        v9x_offset_key(key, "R", v9x_result.offsets[index]);
        v9x_hex(text, v9x_result.first[index], 8);
        if (v9x_result.second[index] != v9x_result.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_result.second[index], 8);
        }
        WritePrivateProfileStringA("Mmio", key, text, V9X_HSWMM_PATH);
    }
    for (named = 0ul; named < sizeof(v9x_named) / sizeof(v9x_named[0]);
         ++named) {
        v9x_offset_key(key, "Name.", v9x_named[named].offset);
        WritePrivateProfileStringA("Mmio", key, v9x_named[named].name,
                                   V9X_HSWMM_PATH);
    }
}

/*
 * The head of the GTT against stolen memory. The BIOS maps the VBE
 * framebuffer through the aperture, so entry 0 onward should be valid and
 * point at consecutive pages from BDSM; how far that run goes, and where it
 * breaks, is the inventory Phase 3 needs before it places a ring.
 */
static void v9x_publish_gtt(DWORD bdsm)
{
    struct v9x_intel_hsw_pte pte;
    char key[16];
    DWORD index;
    DWORD run;
    DWORD stolen_run;
    DWORD expect;
    DWORD valid;

    for (index = 0ul; index < V9X_HSWMM_GTT_RAW; ++index) {
        lstrcpyA(key, "Entry");
        v9x_hex(key + 5, index, 4);
        v9x_key("Gtt", key, v9x_result.gtt[index]);
    }

    run = 0ul;
    stolen_run = 0ul;
    valid = 0ul;
    expect = bdsm & V9X_INTEL_HSW_BASE_MASK;
    for (index = 0ul; index < V9X_HSWMM_GTT_COPY; ++index) {
        v9x_intel_hsw_decode_pte(v9x_result.gtt[index], &pte);
        if (pte.valid != 0u) {
            ++valid;
        }
        if (run == index && pte.valid != 0u) {
            ++run;
            if (stolen_run + 1ul == run && pte.physical_high == 0u &&
                pte.physical_low == expect) {
                ++stolen_run;
                expect += V9X_INTEL_HSW_PAGE_BYTES;
            }
        }
    }
    v9x_key("Gtt", "CopiedEntries", V9X_HSWMM_GTT_COPY);
    v9x_key("Gtt", "ValidInCopy", valid);
    v9x_key("Gtt", "ValidRunFromZero", run);
    v9x_key("Gtt", "StolenRunFromZero", stolen_run);
    v9x_key("Gtt", "StolenBase", bdsm & V9X_INTEL_HSW_BASE_MASK);
    v9x_intel_hsw_decode_pte(v9x_result.gtt[0], &pte);
    v9x_key("Gtt", "Entry0Cache", pte.cache);
}

/* True if the command line carries the switch, case-insensitively. */
static int v9x_has_switch(const char *name)
{
    const char *line;
    int length;
    int at;
    int index;
    char a;
    char b;

    line = GetCommandLineA();
    length = lstrlenA(name);
    for (at = 0; line[at] != '\0'; ++at) {
        for (index = 0; index < length; ++index) {
            a = line[at + index];
            b = name[index];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (index == length) {
            return 1;
        }
    }
    return 0;
}

static void v9x_publish_forcewake(void)
{
    const char *outcome;
    DWORD status;

    status = v9x_result.status;
    v9x_key("Forcewake", "AckBefore", v9x_result.fw_ack_before);
    v9x_key("Forcewake", "PollsToAck", v9x_result.fw_polls_to_ack);
    v9x_key("Forcewake", "AckAwake", v9x_result.fw_ack_awake);
    v9x_key("Forcewake", "RequestAwake", v9x_result.fw_request_awake);
    v9x_key("Forcewake", "PollsToRelease", v9x_result.fw_polls_to_release);
    v9x_key("Forcewake", "AckAfter", v9x_result.fw_ack_after);
    if ((status & V9X_HSWMM_ST_FW_ALREADY_HELD) != 0ul) {
        outcome = "already-held-nothing-written";
    } else if ((status & V9X_HSWMM_ST_FW_ACK_TIMEOUT) != 0ul) {
        outcome = "never-acknowledged";
    } else if ((status & V9X_HSWMM_ST_FW_ACKED) != 0ul) {
        outcome = "acknowledged";
    } else {
        outcome = "not-requested";
    }
    WritePrivateProfileStringA("Forcewake", "Request", outcome,
                               V9X_HSWMM_PATH);
    if ((status & V9X_HSWMM_ST_FW_REQUESTED) != 0ul) {
        WritePrivateProfileStringA("Forcewake", "Release",
            (status & V9X_HSWMM_ST_FW_RELEASED) != 0ul ? "released" :
            "release-not-acknowledged", V9X_HSWMM_PATH);
    }
}

void __stdcall V9xHswMmioEntry(void)
{
    HANDLE device;
    DWORD returned;
    DWORD bar0;
    DWORD bdsm;
    DWORD offset;
    DWORD index;
    BOOL ok;
    int forcewake;

    forcewake = v9x_has_switch("/forcewake");

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA("Report", 0, 0, V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Mmio", 0, 0, V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Gtt", 0, 0, V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Forcewake", 0, 0, V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Report", "Tool", "V9XHSWMM", V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Report", "Build", V9X_BUILD_ID,
                               V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Report", "Access",
                               forcewake ? "mmio-read-plus-forcewake" :
                               "mmio-read-only", V9X_HSWMM_PATH);
    WritePrivateProfileStringA("Report", "Result", "INCOMPLETE",
                               V9X_HSWMM_PATH);
    WritePrivateProfileStringA(0, 0, 0, V9X_HSWMM_PATH);

    /* /bcs is Phase B1 alone: no snapshot around it. */
    if (v9x_has_switch("/bcs")) {
        WritePrivateProfileStringA("Report", "Access", "bcs-ring-phase-b1",
                                   V9X_HSWMM_PATH);
        v9x_bcs_run();
        WritePrivateProfileStringA("Report", "Result", "COMPLETE",
                                   V9X_HSWMM_PATH);
        WritePrivateProfileStringA(0, 0, 0, V9X_HSWMM_PATH);
        ExitProcess(0u);
    }

    /* GTTMMADR low dword; the VxD refuses a base above 4 GB by alignment
     * and the high dword is published for the record. */
    bar0 = v9x_config_read(0x10ul);
    v9x_key("Report", "Bar0", bar0);
    v9x_key("Report", "Bar0High", v9x_config_read(0x14ul));
    bdsm = v9x_config_read(V9X_INTEL_HSW_CFG_BDSM);
    v9x_key("Report", "Bdsm", bdsm);

    v9x_request.base = bar0 & V9X_INTEL_HSW_GTTMMADR_MASK;
    v9x_request.flags = forcewake ? V9X_HSWMM_FORCEWAKE : 0ul;
    v9x_request.count = 0ul;
    for (offset = V9X_RCS_BLOCK_BASE;
         offset < V9X_RCS_BLOCK_BASE + V9X_RCS_BLOCK_BYTES; offset += 4ul) {
        v9x_add(offset);
    }
    for (index = 0ul; index < sizeof(v9x_named) / sizeof(v9x_named[0]);
         ++index) {
        v9x_add(v9x_named[index].offset);
    }

    device = CreateFileA(V9X_HSWMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        WritePrivateProfileStringA("Report", "Result", "VXD-LOAD-FAILED",
                                   V9X_HSWMM_PATH);
        v9x_key("Report", "LoadError", GetLastError());
        ExitProcess(2u);
    }
    ok = DeviceIoControl(device, V9X_HSWMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_HSWMM_MAGIC) {
        WritePrivateProfileStringA("Report", "Result", "DIOC-FAILED",
                                   V9X_HSWMM_PATH);
        v9x_key("Report", "Returned", returned);
        ExitProcess(3u);
    }

    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "Base", v9x_result.base);
    v9x_key("Report", "Accepted", v9x_result.accepted);
    v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
    v9x_key("Report", "GttValidWhole", v9x_result.gtt_valid);

    if ((v9x_result.status & V9X_HSWMM_ST_SNAPSHOT_TAKEN) != 0ul) {
        v9x_publish_mmio();
    }
    if (forcewake) {
        v9x_publish_forcewake();
    }
    if ((v9x_result.status & V9X_HSWMM_ST_GTT_READ) != 0ul) {
        v9x_publish_gtt(bdsm);
    }

    WritePrivateProfileStringA("Report", "Result",
        (v9x_result.status & V9X_HSWMM_ST_SNAPSHOT_TAKEN) != 0ul ?
        "COMPLETE" : "NO-SNAPSHOT", V9X_HSWMM_PATH);
    WritePrivateProfileStringA(0, 0, 0, V9X_HSWMM_PATH);
    ExitProcess(0u);
}
