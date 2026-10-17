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
#include "velocity9x/intel_hsw_eu.h"

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
    if (v9x_intel_hsw_xy_color_blt(&batch, 4u, V9X_BCS_PITCH_FIELD, 0u, 0u,
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

/*
 * Phases C (/rcs) and D (/draw): docs\plans\intel-haswell-direct3d.md.
 * Layouts match hsw_mmio_probe.asm's HSW_RCS_* definitions. The region
 * sits at aperture 1820000h, clear of the B2 driver's live BCS ring and
 * status page at 1800000h-1801FFFh.
 */
#define V9X_RCS_DIOC         3u
#define V9X_RCS_MAGIC        0x43525348ul
#define V9X_RCS_OFFSET       0x01820000ul
#define V9X_RCS_STATUS       (V9X_RCS_OFFSET + 0x1000ul)
#define V9X_RCS_IMAGE        (V9X_RCS_OFFSET + 0x2000ul)
#define V9X_RCS_STATE        (V9X_RCS_OFFSET + 0x4000ul)
#define V9X_RCS_STATE_BYTES  0x2000ul
#define V9X_RCS_RT0          (V9X_RCS_OFFSET + 0x6000ul)
#define V9X_RCS_RT1          (V9X_RCS_OFFSET + 0xa000ul)
#define V9X_RCS_BATCH_B      (V9X_RCS_IMAGE + 0x40ul)
#define V9X_RCS_BATCH_D1     (V9X_RCS_IMAGE + 0x400ul)
#define V9X_RCS_BATCH_D2     (V9X_RCS_IMAGE + 0xc00ul)
#define V9X_RCS_BATCH_BYTES  0x800ul
#define V9X_RCS_IMAGE_DW     4096ul
#define V9X_RCS_RING_DW      64ul
#define V9X_RCS_PAGES        15u
#define V9X_RCS_SEGS         4u
#define V9X_RCS_RT_SIDE      64u
#define V9X_RCS_RT_PITCH     256ul
#define V9X_RCS_RT_PIXELS    (V9X_RCS_RT_SIDE * V9X_RCS_RT_SIDE)
#define V9X_RCS_GUARD        0xa5a5a5a5ul
#define V9X_RCS_STORE_RING   0xc0de0011ul
#define V9X_RCS_STORE_BATCH  0xc0de0021ul
#define V9X_RCS_STORE_RETURN 0xc0de0022ul
#define V9X_RCS_BREADCRUMB   0xc0de0031ul
#define V9X_RCS_STORE_AFTER  0xc0de0032ul
#define V9X_RCS_D1_CRUMB     0xc0de0041ul
#define V9X_RCS_D1_AFTER     0xc0de0042ul
#define V9X_RCS_D2_CRUMB     0xc0de0051ul
#define V9X_RCS_D2_AFTER     0xc0de0052ul
#define V9X_RCS_ST_BORROWED  0x00000400ul
#define V9X_RCS_ST_REQUESTED 0x00008000ul

struct v9x_rcs_request {
    DWORD base;
    DWORD aperture;
    DWORD bdsm;
    DWORD offset;
    DWORD segments;
    DWORD seg_dwords[V9X_RCS_SEGS];
    DWORD seg_wait[V9X_RCS_SEGS];
    DWORD seg_value[V9X_RCS_SEGS];
    DWORD image_dwords;
    DWORD ring[V9X_RCS_RING_DW];
    DWORD image[V9X_RCS_IMAGE_DW];
};

struct v9x_rcs_result {
    DWORD magic;
    DWORD status;
    DWORD stage;
    DWORD ack_before;
    DWORD polls_ack;
    DWORD ack_after;
    DWORD polls_release;
    DWORD ctl_before;
    DWORD gfx_mode;
    DWORD mi_mode;
    DWORD instpm;
    DWORD start_rb;
    DWORD ctl_rb;
    DWORD head_rb;
    DWORD tail_rb;
    DWORD polls[V9X_RCS_SEGS];
    DWORD heads[V9X_RCS_SEGS];
    DWORD acthd;
    DWORD ipeir;
    DWORD ipehr;
    DWORD instdone;
    DWORD bbaddr;
    DWORD eir;
    DWORD ctl_after;
    DWORD start_after;
    DWORD head_after;
    DWORD tail_after;
    DWORD cpu_readback;
    DWORD bad_pte;
    DWORD ptes[V9X_RCS_PAGES];
    DWORD status_page[16];
    DWORD segs_done;
    DWORD reserved[96 - 67];
    DWORD rt[2][V9X_RCS_RT_PIXELS];
};

typedef char v9x_rcs_layout_matches[
    offsetof(struct v9x_rcs_request, ring) == 72u &&
    offsetof(struct v9x_rcs_request, image) == 328u &&
    offsetof(struct v9x_rcs_result, polls) == 60u &&
    offsetof(struct v9x_rcs_result, heads) == 76u &&
    offsetof(struct v9x_rcs_result, acthd) == 92u &&
    offsetof(struct v9x_rcs_result, ptes) == 140u &&
    offsetof(struct v9x_rcs_result, status_page) == 200u &&
    offsetof(struct v9x_rcs_result, segs_done) == 264u &&
    offsetof(struct v9x_rcs_result, rt) == 384u ? 1 : -1];

static struct v9x_rcs_request v9x_rcs_in;
static struct v9x_rcs_result v9x_rcs_out;
static const char *v9x_rcs_section = "Rcs";

static void v9x_key(const char *section, const char *key, DWORD value);

static void v9x_rcs_key(const char *key, DWORD value)
{
    v9x_key(v9x_rcs_section, key, value);
}

static void v9x_rcs_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(v9x_rcs_section, key, value, V9X_HSWMM_PATH);
}

/* A ring segment passes the checker as a batch once an end is appended. */
static int v9x_rcs_check_segment(const DWORD *dw, DWORD count)
{
    DWORD probe[V9X_RCS_RING_DW + 2u];
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        probe[index] = dw[index];
    }
    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)probe, V9X_RCS_RING_DW + 2u);
    batch.used = count;
    v9x_intel_hsw_mi_batch_buffer_end(&batch);
    v9x_intel_hsw_mi_noop(&batch);
    return v9x_intel_hsw_3d_check((v9x_u32 *)probe, batch.used, &check) ==
           V9X_STATUS_OK;
}

static v9x_u32 *v9x_rcs_image_at(DWORD graphics)
{
    return (v9x_u32 *)(v9x_rcs_in.image + (graphics - V9X_RCS_IMAGE) / 4ul);
}

/* One ring segment: a batch start (or nothing), then a store the VxD
 * waits for. */
static void v9x_rcs_segment(struct v9x_hsw_batch *ring, DWORD seg,
                            DWORD batch, DWORD wait, DWORD value)
{
    DWORD at;

    at = ring->used;
    if (batch != 0ul) {
        v9x_intel_hsw_mi_batch_buffer_start(ring, batch);
    }
    v9x_intel_hsw_mi_store_data_imm(ring, V9X_RCS_STATUS + wait, value);
    v9x_rcs_in.seg_dwords[seg] = ring->used - at;
    v9x_rcs_in.seg_wait[seg] = wait;
    v9x_rcs_in.seg_value[seg] = value;
}

/*
 * The draw triangle in quarter pixels, so the reference below stays in
 * integers: (8.25, 8.25), (55.75, 10.25), (10.25, 55.75); red, green,
 * blue. The vertex buffer carries the same values as floats.
 */
static const long v9x_tri_x[3] = { 33l, 223l, 41l };
static const long v9x_tri_y[3] = { 33l, 41l, 223l };
static const DWORD v9x_tri_vertices[18] = {
    0x41040000ul, 0x41040000ul, 0x3f800000ul, 0ul, 0ul, 0x3f800000ul,
    0x425f0000ul, 0x41240000ul, 0ul, 0x3f800000ul, 0ul, 0x3f800000ul,
    0x41240000ul, 0x425f0000ul, 0ul, 0ul, 0x3f800000ul, 0x3f800000ul
};
/* D1's colour: R 1.0, G 0.2, B 0.6, A 1.0, which is FFFF3399h. */
static const DWORD v9x_d1_rgba[4] = {
    0x3f800000ul, 0x3e4ccccdul, 0x3f19999aul, 0x3f800000ul
};
#define V9X_D1_PIXEL 0xffff3399ul

/*
 * The image: batch A (one store) at its base, the init batch at +40h,
 * the two draw batches at +400h and +C00h (draw only), the state at
 * +2000h (graphics V9X_RCS_STATE) with the kernels and vertices inside
 * it. The ring: a store; batch A then a store; the init batch then a
 * store; for /draw, each draw then a store. Each piece is built with the
 * host-tested builders and checked before the VxD sees it.
 */
static int v9x_shade_build(DWORD pair);
static int v9x_depth_build(void);
#define V9X_DEPTH_MODE      8

/* mode: 0 /rcs, 1 /draw, 2-7 /shade:0-5, 8 /depth. */
static int v9x_rcs_build(int draw)
{
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    struct v9x_hsw_draw d;
    v9x_u32 rt[2];
    v9x_u32 used;
    DWORD at;
    DWORD seg;
    DWORD n;

    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)v9x_rcs_in.image, 16ul);
    if (v9x_intel_hsw_mi_store_data_imm(&batch, V9X_RCS_STATUS + 8ul,
                                        V9X_RCS_STORE_BATCH) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_mi_batch_buffer_end(&batch) != V9X_STATUS_OK ||
        v9x_intel_hsw_mi_noop(&batch) != V9X_STATUS_OK ||
        v9x_intel_hsw_3d_check((v9x_u32 *)v9x_rcs_in.image, batch.used,
                               &check) != V9X_STATUS_OK) {
        return 0;
    }

    v9x_intel_hsw_batch_init(&batch, v9x_rcs_image_at(V9X_RCS_BATCH_B),
                             (V9X_RCS_BATCH_D1 - V9X_RCS_BATCH_B) / 4ul);
    if (v9x_intel_hsw_init_workload(&batch, V9X_RCS_STATE,
                                    V9X_RCS_STATE_BYTES,
                                    V9X_RCS_STATUS + 16ul,
                                    V9X_RCS_BREADCRUMB) != V9X_STATUS_OK ||
        v9x_intel_hsw_3d_check(batch.dw, batch.used, &check) !=
            V9X_STATUS_OK) {
        return 0;
    }
    v9x_rcs_key("InitBatchDwords", batch.used);

    if (v9x_intel_hsw_init_state(v9x_rcs_image_at(V9X_RCS_STATE),
                                 V9X_RCS_STATE_BYTES / 4ul) !=
        V9X_STATUS_OK) {
        return 0;
    }

    if (draw == V9X_DEPTH_MODE && !v9x_depth_build()) {
        return 0;
    }
    if (draw >= 2 && draw < V9X_DEPTH_MODE &&
        !v9x_shade_build((DWORD)(draw - 2))) {
        return 0;
    }
    if (draw == 1) {
        rt[0] = V9X_RCS_RT0;
        rt[1] = V9X_RCS_RT1;
        if (v9x_intel_hsw_draw_state(v9x_rcs_image_at(V9X_RCS_STATE),
                                     V9X_RCS_STATE_BYTES / 4ul, rt,
                                     V9X_RCS_RT_SIDE, V9X_RCS_RT_SIDE,
                                     V9X_RCS_RT_PITCH,
                                     (const v9x_u32 *)v9x_tri_vertices) !=
                V9X_STATUS_OK ||
            v9x_intel_hsw_ps_kernel_constant(
                v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_DRAW_KERNEL(0)),
                V9X_HSW_DRAW_KERNEL_BYTES / 4ul,
                (const v9x_u32 *)v9x_d1_rgba, &used) != V9X_STATUS_OK ||
            v9x_intel_hsw_ps_kernel_interpolated(
                v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_DRAW_KERNEL(1)),
                V9X_HSW_DRAW_KERNEL_BYTES / 4ul, 4u, &used) !=
                V9X_STATUS_OK) {
            return 0;
        }
        for (n = 0ul; n < 2ul; ++n) {
            d.binding_table = V9X_HSW_DRAW_BINDING_TABLE(n);
            d.kernel = V9X_HSW_DRAW_KERNEL(n);
            d.vertices = V9X_RCS_STATE + V9X_HSW_DRAW_VERTICES;
            d.width = V9X_RCS_RT_SIDE;
            d.height = V9X_RCS_RT_SIDE;
            d.attributes = 1u;
            d.blend = 0ul;
            d.cc = 0ul;
            d.sampler = 0ul;
            d.depth_buffer = 0ul;
            d.depth_clear = 0ul;
            d.depth_test = 0ul;
            d.tl_vertices = 0u;
            d.vertex_count = 0ul;
            d.flat = 0u;
            v9x_intel_hsw_batch_init(
                &batch,
                v9x_rcs_image_at(n == 0ul ? V9X_RCS_BATCH_D1 :
                                            V9X_RCS_BATCH_D2),
                V9X_RCS_BATCH_BYTES / 4ul);
            if (v9x_intel_hsw_draw_workload(
                    &batch, V9X_RCS_STATE, V9X_RCS_STATE_BYTES, &d,
                    V9X_RCS_STATUS + (n == 0ul ? 32ul : 48ul),
                    n == 0ul ? V9X_RCS_D1_CRUMB : V9X_RCS_D2_CRUMB) !=
                    V9X_STATUS_OK ||
                v9x_intel_hsw_3d_check(batch.dw, batch.used, &check) !=
                    V9X_STATUS_OK) {
                return 0;
            }
            v9x_rcs_key(n == 0ul ? "Draw1BatchDwords" : "Draw2BatchDwords",
                        batch.used);
        }
    }
    v9x_rcs_in.image_dwords = V9X_RCS_IMAGE_DW;

    v9x_intel_hsw_batch_init(&batch, (v9x_u32 *)v9x_rcs_in.ring,
                             V9X_RCS_RING_DW);
    v9x_rcs_segment(&batch, 0ul, 0ul, 0ul, V9X_RCS_STORE_RING);
    v9x_rcs_segment(&batch, 1ul, V9X_RCS_IMAGE, 12ul, V9X_RCS_STORE_RETURN);
    v9x_rcs_segment(&batch, 2ul, V9X_RCS_BATCH_B, 24ul, V9X_RCS_STORE_AFTER);
    v9x_rcs_in.segments = 3ul;
    if (draw) {
        v9x_rcs_segment(&batch, 3ul, V9X_RCS_BATCH_D1, 40ul,
                        V9X_RCS_D1_AFTER);
        /* D2 rides in the same last segment: the VxD has four. The depth
         * run has one draw. */
        at = batch.used;
        if (draw != V9X_DEPTH_MODE) {
            v9x_intel_hsw_mi_batch_buffer_start(&batch, V9X_RCS_BATCH_D2);
            v9x_intel_hsw_mi_store_data_imm(&batch, V9X_RCS_STATUS + 56ul,
                                            V9X_RCS_D2_AFTER);
            v9x_rcs_in.seg_dwords[3] += batch.used - at;
            v9x_rcs_in.seg_wait[3] = 56ul;
            v9x_rcs_in.seg_value[3] = V9X_RCS_D2_AFTER;
        }
        v9x_rcs_in.segments = 4ul;
    }
    if (batch.overflow != 0u) {
        return 0;
    }
    at = 0ul;
    for (seg = 0ul; seg < v9x_rcs_in.segments; ++seg) {
        if ((v9x_rcs_in.seg_dwords[seg] & 1ul) != 0ul ||
            !v9x_rcs_check_segment(v9x_rcs_in.ring + at,
                                   v9x_rcs_in.seg_dwords[seg])) {
            return 0;
        }
        at += v9x_rcs_in.seg_dwords[seg];
    }
    return 1;
}

static char v9x_hex_digit(DWORD value)
{
    return "0123456789ABCDEF"[value & 15ul];
}

static long v9x_edge(DWORD a, DWORD b, long px, long py)
{
    return (v9x_tri_x[b] - v9x_tri_x[a]) * (py - v9x_tri_y[a]) -
           (v9x_tri_y[b] - v9x_tri_y[a]) * (px - v9x_tri_x[a]);
}

static long v9x_isqrt(long value)
{
    long root = 0l;

    while ((root + 1l) * (root + 1l) <= value) {
        ++root;
    }
    return root;
}

static DWORD v9x_channel(DWORD pixel, DWORD shift)
{
    return (pixel >> shift) & 0xfful;
}

/*
 * The reference: pixel centres (x + 0.5, y + 0.5), Pixel Location 0 of
 * 3DSTATE_MULTISAMPLE. A centre inside all three edges is covered; one
 * within 1/64 pixel of an edge is counted apart and not judged, so the
 * fill convention on edges is not what this measures. Colour by
 * barycentric weight, rounded to UNORM8.
 */
static void v9x_rcs_judge(DWORD target, int interpolated)
{
    const DWORD *pixels = v9x_rcs_out.rt[target];
    char prefix[8];
    char key[32];
    long area;
    long len[3];
    long e[3];
    long px;
    long py;
    DWORD x;
    DWORD y;
    DWORD i;
    DWORD got;
    DWORD want;
    DWORD inside = 0ul;
    DWORD outside = 0ul;
    DWORD edge = 0ul;
    DWORD coverage_bad = 0ul;
    DWORD inside_guard = 0ul;
    DWORD colour_bad = 0ul;
    DWORD max_error = 0ul;
    DWORD error;
    DWORD first_bad = 0xfffffffful;
    DWORD first_got = 0ul;
    int on_edge;
    int in;

    lstrcpyA(prefix, interpolated ? "D2" : "D1");
    area = v9x_edge(0ul, 1ul, v9x_tri_x[2], v9x_tri_y[2]);
    for (i = 0ul; i < 3ul; ++i) {
        DWORD j = (i + 1ul) % 3ul;
        long dx = v9x_tri_x[j] - v9x_tri_x[i];
        long dy = v9x_tri_y[j] - v9x_tri_y[i];

        len[i] = v9x_isqrt(dx * dx + dy * dy);
    }
    for (y = 0ul; y < V9X_RCS_RT_SIDE; ++y) {
        for (x = 0ul; x < V9X_RCS_RT_SIDE; ++x) {
            px = (long)x * 4l + 2l;
            py = (long)y * 4l + 2l;
            on_edge = 0;
            in = 1;
            for (i = 0ul; i < 3ul; ++i) {
                e[i] = v9x_edge(i, (i + 1ul) % 3ul, px, py);
                if ((e[i] < 0l ? -e[i] : e[i]) * 16l < len[i]) {
                    on_edge = 1;
                }
                if ((area > 0l && e[i] < 0l) || (area < 0l && e[i] > 0l)) {
                    in = 0;
                }
            }
            got = pixels[y * V9X_RCS_RT_SIDE + x];
            if (on_edge) {
                ++edge;
                continue;
            }
            if (!in) {
                ++outside;
                if (got != V9X_RCS_GUARD) {
                    ++coverage_bad;
                    if (first_bad == 0xfffffffful) {
                        first_bad = (y << 16) | x;
                        first_got = got;
                    }
                }
                continue;
            }
            ++inside;
            if (got == V9X_RCS_GUARD) {
                ++coverage_bad;
                ++inside_guard;
                if (first_bad == 0xfffffffful) {
                    first_bad = (y << 16) | x;
                    first_got = got;
                }
                continue;
            }
            if (!interpolated) {
                want = V9X_D1_PIXEL;
            } else {
                /* Weight of vertex k is the edge opposite it over the
                 * area: e[1] for vertex 0, e[2] for 1, e[0] for 2. */
                want = 0xff000000ul |
                       ((DWORD)((510l * e[1] + area) / (2l * area)) << 16) |
                       ((DWORD)((510l * e[2] + area) / (2l * area)) << 8) |
                       (DWORD)((510l * e[0] + area) / (2l * area));
            }
            error = 0ul;
            for (i = 0ul; i < 32ul; i += 8ul) {
                DWORD a = v9x_channel(got, i);
                DWORD b = v9x_channel(want, i);
                DWORD diff = a > b ? a - b : b - a;

                if (diff > error) {
                    error = diff;
                }
            }
            if (error > max_error) {
                max_error = error;
            }
            if (error > (interpolated ? 2ul : 0ul)) {
                ++colour_bad;
                if (first_bad == 0xfffffffful) {
                    first_bad = (y << 16) | x;
                    first_got = got;
                }
            }
        }
    }

    lstrcpyA(key, prefix);
    lstrcatA(key, "Inside");
    v9x_rcs_key(key, inside);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Outside");
    v9x_rcs_key(key, outside);
    lstrcpyA(key, prefix);
    lstrcatA(key, "EdgeNotJudged");
    v9x_rcs_key(key, edge);
    lstrcpyA(key, prefix);
    lstrcatA(key, "CoverageMismatches");
    v9x_rcs_key(key, coverage_bad);
    lstrcpyA(key, prefix);
    lstrcatA(key, "ColourMismatches");
    v9x_rcs_key(key, colour_bad);
    lstrcpyA(key, prefix);
    lstrcatA(key, "MaxChannelError");
    v9x_rcs_key(key, max_error);
    lstrcpyA(key, prefix);
    lstrcatA(key, "FirstBadYX");
    v9x_rcs_key(key, first_bad);
    lstrcpyA(key, prefix);
    lstrcatA(key, "FirstBadPixel");
    v9x_rcs_key(key, first_got);
    /* Three sample pixels: near each vertex, and the centre of mass. */
    lstrcpyA(key, prefix);
    lstrcatA(key, "Pixel_12_12");
    v9x_rcs_key(key, pixels[12ul * V9X_RCS_RT_SIDE + 12ul]);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Pixel_48_14");
    v9x_rcs_key(key, pixels[14ul * V9X_RCS_RT_SIDE + 48ul]);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Pixel_14_48");
    v9x_rcs_key(key, pixels[48ul * V9X_RCS_RT_SIDE + 14ul]);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Pixel_24_24");
    v9x_rcs_key(key, pixels[24ul * V9X_RCS_RT_SIDE + 24ul]);
    lstrcpyA(key, prefix);
    lstrcatA(key, "Verdict");
    v9x_rcs_text(key,
        inside == 0ul ? "no-pixels-judged" :
        inside_guard == inside && coverage_bad == inside ? "nothing-drawn" :
        coverage_bad == 0ul && colour_bad == 0ul ? "pass" :
        coverage_bad == 0ul ? "coverage-ok-colour-wrong" : "coverage-wrong");
}

/*
 * Phase E (/shade:N): two variants per run in D's layout, eight in all.
 * Same triangle as D. Per vertex, as 0/1 values so the reference stays in
 * integers: diffuse white / red / blue, all alpha 1; specular alpha (the
 * fog factor) 1 / 0 / 1; texture coordinates 0 (0,0) / (1,0) / (0,1) and
 * 1 (0,1) / (0,0) / (1,0).
 */
#define V9X_SHADE_VARIANTS   12u
/* Sampler kinds (Phase F1a); 0 is E's nearest CLAMP. */
#define V9X_SHADE_NEAREST    0u
#define V9X_SHADE_BILINEAR   1u
#define V9X_SHADE_WRAP       2u
#define V9X_SHADE_MIRROR     3u
#define V9X_SHADE_GUARD_BYTE 0xa5ul
#define V9X_SHADE_FLOAT_ONE  0x3f800000ul
#define V9X_SHADE_ALPHA_REF  0x80u
#define V9X_SHADE_TEX_SIDE   8l

struct v9x_shade_variant {
    const char *name;
    v9x_u8 stage0;
    v9x_u8 stage1;
    v9x_u8 fog;
    v9x_u8 alpha_test;
    v9x_u8 sampler_kind; /* V9X_SHADE_NEAREST.. */
    v9x_u8 blend_alpha;  /* SRC_ALPHA / INV_SRC_ALPHA over the guard */
    v9x_u8 uv_scale;     /* texture coordinates 0..uv_scale; 0 means 1 */
};

static const struct v9x_shade_variant v9x_shade_variants[V9X_SHADE_VARIANTS] = {
    { "Texture", V9X_HSW_PS_TEXTURE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u },
    { "Modulate", V9X_HSW_PS_MODULATE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u },
    { "Add", V9X_HSW_PS_ADD, V9X_HSW_PS_STAGE1_NONE, 0u, 0u },
    { "Decal", V9X_HSW_PS_DECAL, V9X_HSW_PS_STAGE1_NONE, 0u, 0u },
    { "AlphaTest", V9X_HSW_PS_MODULATE, V9X_HSW_PS_STAGE1_NONE, 0u, 1u },
    { "Fog", V9X_HSW_PS_DIFFUSE, V9X_HSW_PS_STAGE1_NONE, 1u, 0u },
    { "TwoStage", V9X_HSW_PS_MODULATE, V9X_HSW_PS_MODULATE_TEX1, 0u, 0u },
    { "Diffuse", V9X_HSW_PS_DIFFUSE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u },
    /* Phase F1a. Pairs share vertices, so the 0..2 coordinates pair. */
    { "Wrap", V9X_HSW_PS_TEXTURE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u,
      V9X_SHADE_WRAP, 0u, 2u },
    { "Mirror", V9X_HSW_PS_TEXTURE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u,
      V9X_SHADE_MIRROR, 0u, 2u },
    { "Bilinear", V9X_HSW_PS_TEXTURE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u,
      V9X_SHADE_BILINEAR, 0u, 1u },
    { "Blend", V9X_HSW_PS_TEXTURE, V9X_HSW_PS_STAGE1_NONE, 0u, 0u,
      V9X_SHADE_NEAREST, 1u, 1u }
};

static long v9x_shade_scale(const struct v9x_shade_variant *v)
{
    return v->uv_scale != 0u ? (long)v->uv_scale : 1l;
}

/* 0, 1.0 or 2.0 as a float. */
static DWORD v9x_shade_coord(v9x_u8 bit, long scale)
{
    if (bit == 0u) {
        return 0ul;
    }
    return scale == 2l ? 0x40000000ul : 0x3f800000ul;
}

/* Per vertex: diffuse R G B A, specular A, u0 v0, u1 v1, all 0 or 1. */
static const v9x_u8 v9x_shade_diffuse[3][4] = {
    { 1u, 1u, 1u, 1u }, { 1u, 0u, 0u, 1u }, { 0u, 0u, 1u, 1u }
};
static const v9x_u8 v9x_shade_fog_factor[3] = { 1u, 0u, 1u };
static const v9x_u8 v9x_shade_uv[2][3][2] = {
    { { 0u, 0u }, { 1u, 0u }, { 0u, 1u } },
    { { 0u, 1u }, { 0u, 0u }, { 1u, 0u } }
};
/* Fog colour 0.2, 0.4, 0.6: 51, 102, 153 as UNORM8. */
static const DWORD v9x_shade_fog_rgb[3] = {
    0x3e4ccccdul, 0x3ecccccdul, 0x3f19999aul
};
static const DWORD v9x_shade_fog_255[3] = { 51ul, 102ul, 153ul };

static DWORD v9x_shade_bit(v9x_u8 value)
{
    return value != 0u ? V9X_SHADE_FLOAT_ONE : 0ul;
}

/* Texture 0: B by column, G by row, R falling along the diagonal, alpha
 * C0h or 40h in a checkerboard so the alpha test passes half. Texture 1:
 * pale, alpha FFh. */
static DWORD v9x_shade_texel(DWORD t, long x, long y)
{
    if (t == 0ul) {
        return (((x ^ y) & 1l) != 0l ? 0xc0000000ul : 0x40000000ul) |
               ((DWORD)(0xff - (x + y) * 16) << 16) |
               ((DWORD)(y * 32 + 16) << 8) | (DWORD)(x * 32 + 16);
    }
    return 0xff000000ul | ((DWORD)(0x80 + x * 16) << 16) | 0x0000ff00ul |
           (DWORD)(0x80 + y * 16);
}

static int v9x_shade_build(DWORD pair)
{
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    struct v9x_hsw_draw d;
    struct v9x_hsw_ps_desc desc;
    const struct v9x_shade_variant *v;
    v9x_u32 rt[2];
    v9x_u32 used;
    v9x_u32 *at;
    DWORD n;
    DWORD k;
    long x;
    long y;

    rt[0] = V9X_RCS_RT0;
    rt[1] = V9X_RCS_RT1;
    if (v9x_intel_hsw_shade_state(v9x_rcs_image_at(V9X_RCS_STATE),
                                  V9X_RCS_STATE_BYTES / 4ul, V9X_RCS_STATE,
                                  rt, V9X_RCS_RT_SIDE, V9X_RCS_RT_SIDE,
                                  V9X_RCS_RT_PITCH, V9X_SHADE_ALPHA_REF) !=
        V9X_STATUS_OK) {
        return 0;
    }
    for (n = 0ul; n < 2ul; ++n) {
        at = v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_SHADE_TEXTURE(n));
        for (y = 0l; y < V9X_SHADE_TEX_SIDE; ++y) {
            for (x = 0l; x < V9X_SHADE_TEX_SIDE; ++x) {
                at[y * V9X_SHADE_TEX_SIDE + x] = v9x_shade_texel(n, x, y);
            }
        }
    }
    at = v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_SHADE_VERTICES);
    for (k = 0ul; k < 3ul; ++k) {
        at[0] = v9x_tri_vertices[k * 6ul];
        at[1] = v9x_tri_vertices[k * 6ul + 1ul];
        for (n = 0ul; n < 4ul; ++n) {
            at[2ul + n] = v9x_shade_bit(v9x_shade_diffuse[k][n]);
        }
        at[6] = 0ul;
        at[7] = 0ul;
        at[8] = 0ul;
        at[9] = v9x_shade_bit(v9x_shade_fog_factor[k]);
        n = (DWORD)v9x_shade_scale(&v9x_shade_variants[pair * 2ul]);
        at[10] = v9x_shade_coord(v9x_shade_uv[0][k][0], (long)n);
        at[11] = v9x_shade_coord(v9x_shade_uv[0][k][1], (long)n);
        at[12] = v9x_shade_coord(v9x_shade_uv[1][k][0], (long)n);
        at[13] = v9x_shade_coord(v9x_shade_uv[1][k][1], (long)n);
        at += V9X_HSW_SHADE_VERTEX_BYTES / 4u;
    }
    at = v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_SHADE_VERTICES);
    v9x_rcs_key("Vertex1U0", at[14ul + 10ul]);
    v9x_rcs_key("Vertex2V0", at[28ul + 11ul]);

    for (n = 0ul; n < 2ul; ++n) {
        v = &v9x_shade_variants[pair * 2ul + n];
        desc.stage0 = v->stage0;
        desc.stage1 = v->stage1;
        desc.fog = v->fog;
        desc.specular = 0u;
        desc.fog_rgb[0] = v9x_shade_fog_rgb[0];
        desc.fog_rgb[1] = v9x_shade_fog_rgb[1];
        desc.fog_rgb[2] = v9x_shade_fog_rgb[2];
        if (v9x_intel_hsw_ps_kernel_build(
                v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_SHADE_KERNEL(n)),
                V9X_HSW_SHADE_KERNEL_BYTES / 4ul - 32ul, &desc, &used) !=
            V9X_STATUS_OK) {
            return 0;
        }
        d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(n);
        d.kernel = V9X_HSW_SHADE_KERNEL(n);
        d.vertices = V9X_RCS_STATE + V9X_HSW_SHADE_VERTICES;
        d.width = V9X_RCS_RT_SIDE;
        d.height = V9X_RCS_RT_SIDE;
        d.attributes = V9X_HSW_SHADE_ATTRIBUTES;
        d.blend = v->alpha_test ? V9X_HSW_SHADE_BLEND_ALPHA_TEST :
                  v->blend_alpha ? V9X_HSW_SHADE_BLEND_ALPHA : 0ul;
        d.cc = v->alpha_test ? V9X_HSW_SHADE_CC : 0ul;
        d.sampler = v->sampler_kind == V9X_SHADE_BILINEAR ?
                        V9X_HSW_SHADE_SAMPLER_LINEAR :
                    v->sampler_kind == V9X_SHADE_WRAP ?
                        V9X_HSW_SHADE_SAMPLER_WRAP :
                    v->sampler_kind == V9X_SHADE_MIRROR ?
                        V9X_HSW_SHADE_SAMPLER_MIRROR :
                        V9X_HSW_SHADE_SAMPLER;
        d.depth_buffer = 0ul;
        d.depth_clear = 0ul;
        d.depth_test = 0ul;
        d.tl_vertices = 0u;
        d.vertex_count = 0ul;
        d.flat = 0u;
        v9x_intel_hsw_batch_init(
            &batch,
            v9x_rcs_image_at(n == 0ul ? V9X_RCS_BATCH_D1 : V9X_RCS_BATCH_D2),
            V9X_RCS_BATCH_BYTES / 4ul);
        if (v9x_intel_hsw_draw_workload(
                &batch, V9X_RCS_STATE, V9X_RCS_STATE_BYTES, &d,
                V9X_RCS_STATUS + (n == 0ul ? 32ul : 48ul),
                n == 0ul ? V9X_RCS_D1_CRUMB : V9X_RCS_D2_CRUMB) !=
                V9X_STATUS_OK ||
            v9x_intel_hsw_3d_check(batch.dw, batch.used, &check) !=
                V9X_STATUS_OK) {
            return 0;
        }
        v9x_rcs_key(n == 0ul ? "Kernel1Dwords" : "Kernel2Dwords", used);
    }
    return 1;
}

/* round(255 * num / den) for 0 <= num <= den. */
static DWORD v9x_shade_unorm(long num, long den)
{
    return (DWORD)((510l * num + den) / (2l * den));
}

/* A nearest-texel index along one axis, or -1 when the coordinate lies
 * within 1/64 texel of a boundary (not judged). coord = num / den, at
 * least 0. CLAMP holds the last texel; WRAP repeats every texture width;
 * MIRROR reflects every other repeat (Vol 2a Texture Coordinate Mode). */
static long v9x_shade_texel_index(long num, long den, v9x_u8 kind)
{
    long scaled = num * V9X_SHADE_TEX_SIDE;
    long index = scaled / den;
    long frac = scaled - index * den;
    long period;

    if (frac * 64l < den || frac * 64l > den * 63l) {
        return -1l;
    }
    if (kind == V9X_SHADE_WRAP) {
        return index % V9X_SHADE_TEX_SIDE;
    }
    if (kind == V9X_SHADE_MIRROR) {
        period = index % (2l * V9X_SHADE_TEX_SIDE);
        return period < V9X_SHADE_TEX_SIDE ? period :
                                             2l * V9X_SHADE_TEX_SIDE - 1l -
                                                 period;
    }
    return index >= V9X_SHADE_TEX_SIDE ? V9X_SHADE_TEX_SIDE - 1l : index;
}

static long v9x_shade_clamp_texel(long index)
{
    if (index < 0l) {
        return 0l;
    }
    return index >= V9X_SHADE_TEX_SIDE ? V9X_SHADE_TEX_SIDE - 1l : index;
}

/* Bilinear under CLAMP: sample point coord * 8 - 0.5, weights as 8-bit
 * fractions, rounded. */
static DWORD v9x_shade_bilinear(DWORD t, const long uv[2], long den)
{
    long i0[2];
    long f8[2];
    long s;
    long fl;
    DWORD c;
    DWORD out = 0ul;
    DWORD t00;
    DWORD t10;
    DWORD t01;
    DWORD t11;
    long a;
    DWORD axis;

    for (axis = 0ul; axis < 2ul; ++axis) {
        s = uv[axis] * V9X_SHADE_TEX_SIDE * 2l - den;  /* 2 * den * point */
        fl = s >= 0l ? s / (2l * den) : -((-s + 2l * den - 1l) / (2l * den));
        i0[axis] = fl;
        f8[axis] = ((s - fl * 2l * den) * 256l + den) / (2l * den);
    }
    t00 = v9x_shade_texel(t, v9x_shade_clamp_texel(i0[0]),
                          v9x_shade_clamp_texel(i0[1]));
    t10 = v9x_shade_texel(t, v9x_shade_clamp_texel(i0[0] + 1l),
                          v9x_shade_clamp_texel(i0[1]));
    t01 = v9x_shade_texel(t, v9x_shade_clamp_texel(i0[0]),
                          v9x_shade_clamp_texel(i0[1] + 1l));
    t11 = v9x_shade_texel(t, v9x_shade_clamp_texel(i0[0] + 1l),
                          v9x_shade_clamp_texel(i0[1] + 1l));
    for (c = 0ul; c < 32ul; c += 8ul) {
        a = ((long)((t00 >> c) & 0xfful) * (256l - f8[0]) +
             (long)((t10 >> c) & 0xfful) * f8[0]) * (256l - f8[1]) +
            ((long)((t01 >> c) & 0xfful) * (256l - f8[0]) +
             (long)((t11 >> c) & 0xfful) * f8[0]) * f8[1];
        out |= (DWORD)((a + 32768l) / 65536l) << c;
    }
    return out;
}

/*
 * The reference for one variant at one covered pixel centre, weights w[k]
 * for vertex k over area. Returns 0 if a texel boundary makes it
 * unjudged; *write is 0 when the alpha test should discard the pixel.
 */
static int v9x_shade_expect(const struct v9x_shade_variant *v,
                            const long w[3], long area, DWORD *want,
                            int *write)
{
    long d[4];
    long uv[2][2];
    long tex_index[2][2];
    DWORD texel[2];
    DWORD c;
    DWORD k;
    DWORD t;
    DWORD out[4];
    DWORD tc;
    DWORD dc;
    DWORD m;
    long f;
    long f256;

    for (c = 0ul; c < 4ul; ++c) {
        d[c] = 0l;
        for (k = 0ul; k < 3ul; ++k) {
            d[c] += w[k] * (long)v9x_shade_diffuse[k][c];
        }
    }
    for (t = 0ul; t < 2ul; ++t) {
        for (c = 0ul; c < 2ul; ++c) {
            uv[t][c] = 0l;
            for (k = 0ul; k < 3ul; ++k) {
                uv[t][c] += w[k] * (long)v9x_shade_uv[t][k][c] *
                            v9x_shade_scale(v);
            }
            tex_index[t][c] = v9x_shade_texel_index(uv[t][c], area,
                                                    v->sampler_kind);
        }
        texel[t] = 0ul;
        if (t == 0ul && v->sampler_kind == V9X_SHADE_BILINEAR) {
            tex_index[t][0] = 0l;
            tex_index[t][1] = 0l;
            texel[t] = v9x_shade_bilinear(t, uv[t], area);
        } else if (tex_index[t][0] >= 0l && tex_index[t][1] >= 0l) {
            texel[t] = v9x_shade_texel(t, tex_index[t][0], tex_index[t][1]);
        }
    }
    if (v->stage0 != V9X_HSW_PS_DIFFUSE &&
        (tex_index[0][0] < 0l || tex_index[0][1] < 0l)) {
        return 0;
    }
    if (v->stage1 == V9X_HSW_PS_MODULATE_TEX1 &&
        (tex_index[1][0] < 0l || tex_index[1][1] < 0l)) {
        return 0;
    }

    /* Channels in B8G8R8A8 order of significance: c = 0 R, 1 G, 2 B, 3 A;
     * the texel's R is bits 23:16. */
    for (c = 0ul; c < 4ul; ++c) {
        DWORD shift = c == 3ul ? 24ul : 16ul - 8ul * c;

        tc = (texel[0] >> shift) & 0xfful;
        dc = v9x_shade_unorm(d[c], area);
        switch (v->stage0) {
        case V9X_HSW_PS_DIFFUSE:
            m = dc;
            break;
        case V9X_HSW_PS_TEXTURE:
            m = tc;
            break;
        case V9X_HSW_PS_MODULATE:
            m = (DWORD)(((long)tc * d[c] * 2l + area) / (2l * area));
            break;
        case V9X_HSW_PS_ADD:
            if (c == 3ul) {
                m = (DWORD)(((long)tc * d[c] * 2l + area) / (2l * area));
            } else {
                m = tc + dc > 255ul ? 255ul : tc + dc;
            }
            break;
        default:
            m = c == 3ul ? dc : tc;
            break;
        }
        if (v->stage1 == V9X_HSW_PS_MODULATE_TEX1) {
            m = (m * ((texel[1] >> shift) & 0xfful) + 127ul) / 255ul;
        }
        if (v->fog && c < 3ul) {
            f = 0l;
            for (k = 0ul; k < 3ul; ++k) {
                f += w[k] * (long)v9x_shade_fog_factor[k];
            }
            f256 = (256l * f + area / 2l) / area;
            m = (DWORD)(((long)m * f256 +
                         (long)v9x_shade_fog_255[c] * (256l - f256) + 128l) /
                        256l);
        }
        out[c] = m;
    }
    /* SRC_ALPHA / INV_SRC_ALPHA, ADD, over the guard byte, alpha with the
     * same factors. The alpha factor is the source alpha before
     * blending. */
    if (v->blend_alpha) {
        m = out[3];
        for (c = 0ul; c < 4ul; ++c) {
            out[c] = (out[c] * m + V9X_SHADE_GUARD_BYTE * (255ul - m) +
                      127ul) / 255ul;
        }
    }
    *want = (out[3] << 24) | (out[0] << 16) | (out[1] << 8) | out[2];
    *write = !v->alpha_test || out[3] > (DWORD)V9X_SHADE_ALPHA_REF;
    return 1;
}

static void v9x_shade_judge(DWORD target, DWORD variant)
{
    const struct v9x_shade_variant *v = &v9x_shade_variants[variant];
    const DWORD *pixels = v9x_rcs_out.rt[target];
    char key[40];
    long area;
    long len[3];
    long e[3];
    long w[3];
    long px;
    long py;
    DWORD x;
    DWORD y;
    DWORD i;
    DWORD got;
    DWORD want;
    DWORD inside = 0ul;
    DWORD outside = 0ul;
    DWORD unjudged = 0ul;
    DWORD discarded = 0ul;
    DWORD bad = 0ul;
    DWORD max_error = 0ul;
    DWORD error;
    DWORD first_bad = 0xfffffffful;
    DWORD first_got = 0ul;
    DWORD first_want = 0ul;
    int on_edge;
    int in;
    int write;

    area = v9x_edge(0ul, 1ul, v9x_tri_x[2], v9x_tri_y[2]);
    for (i = 0ul; i < 3ul; ++i) {
        DWORD j = (i + 1ul) % 3ul;
        long dx = v9x_tri_x[j] - v9x_tri_x[i];
        long dy = v9x_tri_y[j] - v9x_tri_y[i];

        len[i] = v9x_isqrt(dx * dx + dy * dy);
    }
    for (y = 0ul; y < V9X_RCS_RT_SIDE; ++y) {
        for (x = 0ul; x < V9X_RCS_RT_SIDE; ++x) {
            px = (long)x * 4l + 2l;
            py = (long)y * 4l + 2l;
            on_edge = 0;
            in = 1;
            for (i = 0ul; i < 3ul; ++i) {
                e[i] = v9x_edge(i, (i + 1ul) % 3ul, px, py);
                if ((e[i] < 0l ? -e[i] : e[i]) * 16l < len[i]) {
                    on_edge = 1;
                }
                if (e[i] < 0l) {
                    in = 0;
                }
            }
            got = pixels[y * V9X_RCS_RT_SIDE + x];
            if (on_edge) {
                ++unjudged;
                continue;
            }
            want = V9X_RCS_GUARD;
            write = 0;
            if (in) {
                /* The weight of vertex k is the edge opposite it. */
                w[0] = e[1];
                w[1] = e[2];
                w[2] = e[0];
                if (!v9x_shade_expect(v, w, area, &want, &write)) {
                    ++unjudged;
                    continue;
                }
                ++inside;
                if (!write) {
                    ++discarded;
                    want = V9X_RCS_GUARD;
                }
            } else {
                ++outside;
            }
            error = 0ul;
            if (want == V9X_RCS_GUARD || got == V9X_RCS_GUARD) {
                error = want == got ? 0ul : 256ul;
            } else {
                for (i = 0ul; i < 32ul; i += 8ul) {
                    DWORD a = (got >> i) & 0xfful;
                    DWORD b = (want >> i) & 0xfful;
                    DWORD diff = a > b ? a - b : b - a;

                    if (diff > error) {
                        error = diff;
                    }
                }
            }
            if (error > max_error && error != 256ul) {
                max_error = error;
            }
            if (error > 2ul) {
                ++bad;
                if (first_bad == 0xfffffffful) {
                    first_bad = (y << 16) | x;
                    first_got = got;
                    first_want = want;
                }
            }
        }
    }

#define V9X_SHADE_KEY(suffix, value) \
    do { lstrcpyA(key, v->name); lstrcatA(key, suffix); \
         v9x_rcs_key(key, value); } while (0)
    V9X_SHADE_KEY("Inside", inside);
    V9X_SHADE_KEY("Outside", outside);
    V9X_SHADE_KEY("NotJudged", unjudged);
    V9X_SHADE_KEY("AlphaDiscarded", discarded);
    V9X_SHADE_KEY("Mismatches", bad);
    V9X_SHADE_KEY("MaxChannelError", max_error);
    V9X_SHADE_KEY("FirstBadYX", first_bad);
    V9X_SHADE_KEY("FirstBadGot", first_got);
    V9X_SHADE_KEY("FirstBadWant", first_want);
    V9X_SHADE_KEY("Pixel_12_12", pixels[12ul * V9X_RCS_RT_SIDE + 12ul]);
    V9X_SHADE_KEY("Pixel_24_24", pixels[24ul * V9X_RCS_RT_SIDE + 24ul]);
#undef V9X_SHADE_KEY
    lstrcpyA(key, v->name);
    lstrcatA(key, "Verdict");
    v9x_rcs_text(key, inside == 0ul ? "no-pixels-judged" :
                      bad == 0ul ? "pass" : "mismatch");
}

/*
 * Phase F1b (/depth): one draw into target 0 with a D32_FLOAT depth
 * buffer in target 1's memory. The clear triangle covers the target at
 * z = 1.0 in black under ALWAYS; then A (z 0.5, red), B (0.25, green)
 * and C (0.75, blue) under LESS, in that order, all flat colours through
 * D's interpolating kernel. Vertices in texture 0's space, unused here.
 */
#define V9X_DEPTH_VERTICES  V9X_HSW_SHADE_TEXTURE(0)
#define V9X_DEPTH_TRIANGLES 4u

/* Quarter-pixel corners, depth as 1/4 steps (1.0 = 4), colour. */
static const long v9x_depth_x[V9X_DEPTH_TRIANGLES][3] = {
    { 0l, 512l, 0l }, { 16l, 240l, 16l }, { 80l, 248l, 80l },
    { 40l, 248l, 248l }
};
static const long v9x_depth_y[V9X_DEPTH_TRIANGLES][3] = {
    { 0l, 0l, 512l }, { 16l, 16l, 240l }, { 80l, 80l, 248l },
    { 120l, 120l, 248l }
};
static const long v9x_depth_z4[V9X_DEPTH_TRIANGLES] = { 4l, 2l, 1l, 3l };
static const DWORD v9x_depth_colour[V9X_DEPTH_TRIANGLES] = {
    0xff000000ul, 0xffff0000ul, 0xff00ff00ul, 0xff0000fful
};
/* The same as floats: x, y, z per corner. */
static const DWORD v9x_depth_float_xy[V9X_DEPTH_TRIANGLES][6] = {
    { 0ul, 0ul, 0x43000000ul, 0ul, 0ul, 0x43000000ul },
    { 0x40800000ul, 0x40800000ul, 0x42700000ul, 0x40800000ul,
      0x40800000ul, 0x42700000ul },
    { 0x41a00000ul, 0x41a00000ul, 0x42780000ul, 0x41a00000ul,
      0x41a00000ul, 0x42780000ul },
    { 0x41200000ul, 0x41f00000ul, 0x42780000ul, 0x41f00000ul,
      0x42780000ul, 0x42780000ul }
};
static const DWORD v9x_depth_float_z[V9X_DEPTH_TRIANGLES] = {
    0x3f800000ul, 0x3f000000ul, 0x3e800000ul, 0x3f400000ul
};
static const DWORD v9x_depth_float_rgb[V9X_DEPTH_TRIANGLES][3] = {
    { 0ul, 0ul, 0ul }, { 0x3f800000ul, 0ul, 0ul },
    { 0ul, 0x3f800000ul, 0ul }, { 0ul, 0ul, 0x3f800000ul }
};

static int v9x_depth_build(void)
{
    struct v9x_hsw_batch batch;
    struct v9x_hsw_check check;
    struct v9x_hsw_draw d;
    v9x_u32 rt[2];
    v9x_u32 used;
    v9x_u32 *at;
    DWORD t;
    DWORD k;

    rt[0] = V9X_RCS_RT0;
    rt[1] = V9X_RCS_RT1;
    if (v9x_intel_hsw_shade_state(v9x_rcs_image_at(V9X_RCS_STATE),
                                  V9X_RCS_STATE_BYTES / 4ul, V9X_RCS_STATE,
                                  rt, V9X_RCS_RT_SIDE, V9X_RCS_RT_SIDE,
                                  V9X_RCS_RT_PITCH, V9X_SHADE_ALPHA_REF) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_ps_kernel_interpolated(
            v9x_rcs_image_at(V9X_RCS_STATE + V9X_HSW_SHADE_KERNEL(0)),
            V9X_HSW_SHADE_KERNEL_BYTES / 4ul - 32ul, 4u, &used) !=
            V9X_STATUS_OK) {
        return 0;
    }
    at = v9x_rcs_image_at(V9X_RCS_STATE + V9X_DEPTH_VERTICES);
    for (t = 0ul; t < V9X_DEPTH_TRIANGLES; ++t) {
        for (k = 0ul; k < 3ul; ++k) {
            at[0] = v9x_depth_float_xy[t][k * 2ul];
            at[1] = v9x_depth_float_xy[t][k * 2ul + 1ul];
            at[2] = v9x_depth_float_z[t];
            at[3] = v9x_depth_float_rgb[t][0];
            at[4] = v9x_depth_float_rgb[t][1];
            at[5] = v9x_depth_float_rgb[t][2];
            at[6] = 0x3f800000ul;
            at += V9X_HSW_DEPTH_VERTEX_BYTES / 4u;
        }
    }

    d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d.kernel = V9X_HSW_SHADE_KERNEL(0);
    d.vertices = V9X_RCS_STATE + V9X_DEPTH_VERTICES;
    d.width = V9X_RCS_RT_SIDE;
    d.height = V9X_RCS_RT_SIDE;
    d.attributes = 1u;
    d.blend = 0ul;
    d.cc = 0ul;
    d.sampler = 0ul;
    d.depth_buffer = V9X_RCS_RT1;
    d.depth_clear = V9X_HSW_SHADE_DS_ALWAYS;
    d.depth_test = V9X_HSW_SHADE_DS_LESS;
    d.tl_vertices = 0u;
    d.vertex_count = 0ul;
    d.flat = 0u;
    v9x_intel_hsw_batch_init(&batch, v9x_rcs_image_at(V9X_RCS_BATCH_D1),
                             V9X_RCS_BATCH_BYTES / 4ul);
    if (v9x_intel_hsw_draw_workload(&batch, V9X_RCS_STATE,
                                    V9X_RCS_STATE_BYTES, &d,
                                    V9X_RCS_STATUS + 32ul,
                                    V9X_RCS_D1_CRUMB) != V9X_STATUS_OK ||
        v9x_intel_hsw_3d_check(batch.dw, batch.used, &check) !=
            V9X_STATUS_OK) {
        return 0;
    }
    v9x_rcs_key("DepthBatchDwords", batch.used);
    return 1;
}

static long v9x_depth_edge(DWORD t, DWORD a, DWORD b, long px, long py)
{
    return (v9x_depth_x[t][b] - v9x_depth_x[t][a]) *
               (py - v9x_depth_y[t][a]) -
           (v9x_depth_y[t][b] - v9x_depth_y[t][a]) *
               (px - v9x_depth_x[t][a]);
}

/* The reference: each triangle in order, covered centres pass when nearer
 * than the stored depth (the clear passes everywhere). A centre within
 * 1/64 pixel of any edge inside the target is not judged. */
static void v9x_depth_judge(void)
{
    const DWORD *pixels = v9x_rcs_out.rt[0];
    long area;
    long len;
    long dx;
    long dy;
    long e;
    long px;
    long py;
    long depth;
    DWORD want;
    DWORD got;
    DWORD x;
    DWORD y;
    DWORD t;
    DWORD i;
    DWORD judged = 0ul;
    DWORD unjudged = 0ul;
    DWORD bad = 0ul;
    DWORD seen[V9X_DEPTH_TRIANGLES];
    DWORD first_bad = 0xfffffffful;
    DWORD first_got = 0ul;
    DWORD first_want = 0ul;
    int in;
    int near_edge;
    char key[24];

    for (t = 0ul; t < V9X_DEPTH_TRIANGLES; ++t) {
        seen[t] = 0ul;
    }
    for (y = 0ul; y < V9X_RCS_RT_SIDE; ++y) {
        for (x = 0ul; x < V9X_RCS_RT_SIDE; ++x) {
            px = (long)x * 4l + 2l;
            py = (long)y * 4l + 2l;
            depth = 5l;
            want = V9X_RCS_GUARD;
            near_edge = 0;
            for (t = 0ul; t < V9X_DEPTH_TRIANGLES; ++t) {
                area = v9x_depth_edge(t, 0ul, 1ul, v9x_depth_x[t][2],
                                      v9x_depth_y[t][2]);
                in = 1;
                for (i = 0ul; i < 3ul; ++i) {
                    e = v9x_depth_edge(t, i, (i + 1ul) % 3ul, px, py);
                    dx = v9x_depth_x[t][(i + 1ul) % 3ul] - v9x_depth_x[t][i];
                    dy = v9x_depth_y[t][(i + 1ul) % 3ul] - v9x_depth_y[t][i];
                    len = v9x_isqrt(dx * dx + dy * dy);
                    if ((e < 0l ? -e : e) * 16l < len) {
                        near_edge = 1;
                    }
                    if ((area > 0l && e < 0l) || (area < 0l && e > 0l)) {
                        in = 0;
                    }
                }
                if (in && v9x_depth_z4[t] < depth) {
                    depth = v9x_depth_z4[t];
                    want = v9x_depth_colour[t];
                }
            }
            got = pixels[y * V9X_RCS_RT_SIDE + x];
            if (near_edge) {
                ++unjudged;
                continue;
            }
            ++judged;
            for (t = 0ul; t < V9X_DEPTH_TRIANGLES; ++t) {
                if (want == v9x_depth_colour[t]) {
                    ++seen[t];
                }
            }
            if (got != want) {
                ++bad;
                if (first_bad == 0xfffffffful) {
                    first_bad = (y << 16) | x;
                    first_got = got;
                    first_want = want;
                }
            }
        }
    }
    v9x_rcs_key("DepthJudged", judged);
    v9x_rcs_key("DepthNotJudged", unjudged);
    v9x_rcs_key("DepthMismatches", bad);
    for (t = 0ul; t < V9X_DEPTH_TRIANGLES; ++t) {
        lstrcpyA(key, "DepthExpectTri0");
        key[14] = (char)('0' + t);
        v9x_rcs_key(key, seen[t]);
    }
    v9x_rcs_key("DepthFirstBadYX", first_bad);
    v9x_rcs_key("DepthFirstBadGot", first_got);
    v9x_rcs_key("DepthFirstBadWant", first_want);
    v9x_rcs_key("DepthPixel_8_8", pixels[8ul * V9X_RCS_RT_SIDE + 8ul]);
    v9x_rcs_key("DepthPixel_30_30", pixels[30ul * V9X_RCS_RT_SIDE + 30ul]);
    v9x_rcs_key("DepthPixel_50_40", pixels[40ul * V9X_RCS_RT_SIDE + 50ul]);
    v9x_rcs_key("DepthPixel_2_62", pixels[62ul * V9X_RCS_RT_SIDE + 2ul]);
    v9x_rcs_text("DepthVerdict", bad == 0ul ? "pass" : "mismatch");
}

static void v9x_rcs_publish(int draw)
{
    char key[16];
    DWORD index;
    DWORD seen;

    v9x_rcs_key("Status", v9x_rcs_out.status);
    v9x_rcs_key("Stage", v9x_rcs_out.stage);
    v9x_rcs_key("BadPte", v9x_rcs_out.bad_pte);
    v9x_rcs_key("CpuReadback", v9x_rcs_out.cpu_readback);
    v9x_rcs_key("AckBefore", v9x_rcs_out.ack_before);
    v9x_rcs_key("PollsToAck", v9x_rcs_out.polls_ack);
    v9x_rcs_key("GfxMode", v9x_rcs_out.gfx_mode);
    v9x_rcs_key("MiMode", v9x_rcs_out.mi_mode);
    v9x_rcs_key("Instpm", v9x_rcs_out.instpm);
    v9x_rcs_key("CtlBefore", v9x_rcs_out.ctl_before);
    v9x_rcs_key("StartReadback", v9x_rcs_out.start_rb);
    v9x_rcs_key("CtlReadback", v9x_rcs_out.ctl_rb);
    v9x_rcs_key("HeadReadback", v9x_rcs_out.head_rb);
    v9x_rcs_key("TailReadback", v9x_rcs_out.tail_rb);
    v9x_rcs_key("SegmentsDone", v9x_rcs_out.segs_done);
    for (index = 0ul; index < v9x_rcs_in.segments; ++index) {
        lstrcpyA(key, "Polls1");
        key[5] = (char)('1' + index);
        v9x_rcs_key(key, v9x_rcs_out.polls[index]);
        lstrcpyA(key, "Head1");
        key[4] = (char)('1' + index);
        v9x_rcs_key(key, v9x_rcs_out.heads[index]);
    }
    v9x_rcs_key("Acthd", v9x_rcs_out.acthd);
    v9x_rcs_key("Ipeir", v9x_rcs_out.ipeir);
    v9x_rcs_key("Ipehr", v9x_rcs_out.ipehr);
    v9x_rcs_key("Instdone", v9x_rcs_out.instdone);
    v9x_rcs_key("Bbaddr", v9x_rcs_out.bbaddr);
    v9x_rcs_key("Eir", v9x_rcs_out.eir);
    v9x_rcs_key("CtlAfter", v9x_rcs_out.ctl_after);
    v9x_rcs_key("StartAfter", v9x_rcs_out.start_after);
    v9x_rcs_key("HeadAfter", v9x_rcs_out.head_after);
    v9x_rcs_key("TailAfter", v9x_rcs_out.tail_after);
    v9x_rcs_key("PollsToRelease", v9x_rcs_out.polls_release);
    v9x_rcs_key("AckAfter", v9x_rcs_out.ack_after);
    for (index = 0ul; index < V9X_RCS_PAGES; ++index) {
        lstrcpyA(key, "Pte0");
        key[3] = v9x_hex_digit(index);
        v9x_rcs_key(key, v9x_rcs_out.ptes[index]);
    }
    for (index = 0ul; index < 16ul; ++index) {
        lstrcpyA(key, "StatusPage0");
        key[10] = v9x_hex_digit(index);
        v9x_rcs_key(key, v9x_rcs_out.status_page[index]);
    }

    /* One verdict per store, in the order the engine meets them. */
    seen = 0ul;
    if (v9x_rcs_out.status_page[0] == V9X_RCS_STORE_RING) {
        seen |= 1ul;
    }
    if (v9x_rcs_out.status_page[2] == V9X_RCS_STORE_BATCH) {
        seen |= 2ul;
    }
    if (v9x_rcs_out.status_page[3] == V9X_RCS_STORE_RETURN) {
        seen |= 4ul;
    }
    if (v9x_rcs_out.status_page[4] == V9X_RCS_BREADCRUMB &&
        v9x_rcs_out.status_page[5] == 0ul) {
        seen |= 8ul;
    }
    if (v9x_rcs_out.status_page[6] == V9X_RCS_STORE_AFTER) {
        seen |= 16ul;
    }
    if (v9x_rcs_out.status_page[8] == V9X_RCS_D1_CRUMB &&
        v9x_rcs_out.status_page[9] == 0ul) {
        seen |= 32ul;
    }
    if (v9x_rcs_out.status_page[10] == V9X_RCS_D1_AFTER) {
        seen |= 64ul;
    }
    if (v9x_rcs_out.status_page[12] == V9X_RCS_D2_CRUMB &&
        v9x_rcs_out.status_page[13] == 0ul) {
        seen |= 128ul;
    }
    if (v9x_rcs_out.status_page[14] == V9X_RCS_D2_AFTER) {
        seen |= 256ul;
    }
    v9x_rcs_key("StoresSeen", seen);
    v9x_rcs_text("Forcewake",
        (v9x_rcs_out.status & V9X_RCS_ST_BORROWED) != 0ul ?
            "borrowed-from-driver" :
        (v9x_rcs_out.status & V9X_RCS_ST_REQUESTED) != 0ul ? "requested" :
            "none");
    v9x_rcs_text("Verdict",
        (seen & 31ul) != 31ul ?
            ((seen & 1ul) == 0ul ? "ring-store-missing" :
             (seen & 6ul) != 6ul ? "batch-not-run" :
             (seen & 8ul) == 0ul ? "breadcrumb-missing" :
                                   "after-store-missing") :
        !draw ? "init-workload-ran" :
        (seen & 96ul) != 96ul ? "draw1-did-not-complete" :
        draw == V9X_DEPTH_MODE ? "depth-draw-completed" :
        (seen & 384ul) != 384ul ? "draw2-did-not-complete" :
                                  "both-draws-completed");
    if (draw == 1) {
        v9x_rcs_judge(0ul, 0);
        v9x_rcs_judge(1ul, 1);
    } else if (draw == V9X_DEPTH_MODE) {
        v9x_depth_judge();
    } else if (draw >= 2) {
        v9x_shade_judge(0ul, (DWORD)(draw - 2) * 2ul);
        v9x_shade_judge(1ul, (DWORD)(draw - 2) * 2ul + 1ul);
    }
}

static void v9x_rcs_run(int draw)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    v9x_rcs_section = draw == V9X_DEPTH_MODE ? "Depth" :
                      draw >= 2 ? "Shade" : draw ? "Draw" : "Rcs";
    WritePrivateProfileStringA(v9x_rcs_section, 0, 0, V9X_HSWMM_PATH);
    v9x_rcs_in.base = v9x_config_read(0x10ul) & V9X_INTEL_HSW_GTTMMADR_MASK;
    v9x_rcs_in.aperture = v9x_config_read(0x18ul) & 0xfffffff0ul;
    v9x_rcs_in.bdsm = v9x_config_read(V9X_INTEL_HSW_CFG_BDSM);
    v9x_rcs_in.offset = V9X_RCS_OFFSET;
    v9x_rcs_key("Aperture", v9x_rcs_in.aperture);
    v9x_rcs_key("Offset", v9x_rcs_in.offset);
    if (!v9x_rcs_build(draw)) {
        v9x_rcs_text("Result", "COMMANDS-REFUSED");
        return;
    }
    v9x_rcs_key("Segment1Dwords", v9x_rcs_in.seg_dwords[0]);
    v9x_rcs_key("Segment2Dwords", v9x_rcs_in.seg_dwords[1]);
    v9x_rcs_key("Segment3Dwords", v9x_rcs_in.seg_dwords[2]);
    v9x_rcs_key("Segment4Dwords", v9x_rcs_in.seg_dwords[3]);

    device = CreateFileA(V9X_HSWMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_rcs_text("Result", "VXD-LOAD-FAILED");
        return;
    }
    ok = DeviceIoControl(device, V9X_RCS_DIOC, &v9x_rcs_in,
                         sizeof(v9x_rcs_in), &v9x_rcs_out,
                         sizeof(v9x_rcs_out), &returned, 0);
    if (!ok) {
        v9x_rcs_key("DiocError", GetLastError());
    }
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_rcs_out) ||
        v9x_rcs_out.magic != V9X_RCS_MAGIC) {
        v9x_rcs_key("DiocOk", (DWORD)ok);
        v9x_rcs_key("DiocReturned", returned);
        v9x_rcs_key("DiocMagic", v9x_rcs_out.magic);
        v9x_rcs_text("Result", "DIOC-FAILED");
        return;
    }
    v9x_rcs_publish(draw);
    v9x_rcs_text("Result", "COMPLETE");
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

    /* /rcs is Phase C alone, /draw Phase D, /shade:N Phase E, likewise. */
    if (v9x_has_switch("/rcs") || v9x_has_switch("/draw") ||
        v9x_has_switch("/shade:") || v9x_has_switch("/depth")) {
        int mode = v9x_has_switch("/draw") ? 1 :
                   v9x_has_switch("/depth") ? V9X_DEPTH_MODE : 0;

        for (index = 0ul; index < 6ul; ++index) {
            char option[9];

            lstrcpyA(option, "/shade:0");
            option[7] = (char)('0' + index);
            if (v9x_has_switch(option)) {
                mode = 2 + (int)index;
            }
        }
        WritePrivateProfileStringA("Report", "Access",
                                   mode == V9X_DEPTH_MODE ?
                                       "rcs-depth-phase-f1b" :
                                   mode >= 2 ? "rcs-shade-phase-e" :
                                   mode == 1 ? "rcs-draw-phase-d" :
                                               "rcs-ring-phase-c",
                                   V9X_HSWMM_PATH);
        v9x_rcs_run(mode);
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
