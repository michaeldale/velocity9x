/*
 * V9XN5MM.EXE: NVIDIA GT218 (GeForce 210) read-only register snapshot.
 *
 * Phase 2 of docs\plans\nvidia-gt218.md. Finds PCI 10DE:0A65 by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), dumps its
 * configuration space, loads N5MM.VXD and asks it for register offsets
 * through BAR0, each read twice. Writes C:\V9XDIAG\V9XN5MM.INI:
 *
 *   [Report]  build, the device found, the VxD's status, what was taken;
 *   [Config]  configuration space, C<offset>=dword, all 256 bytes;
 *   [Mmio]    every register read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux nouveau;
 *   [Pramin]  PRAMIN window samples, P<window offset>, non-zero words only;
 *   [Decode]  chipset, memory size, type and partitions, the PRAMIN window
 *             and BAR VM state, the engines PMC_ENABLE has on, head 0's
 *             scanout state.
 *
 * Captures in order: the always-present units (PMC, PBUS, PTIMER, PFB,
 * PSTRAPS) and PDISP; then PFIFO, PGRAPH and the copy engine, each only if
 * PMC_ENABLE has it on (the NV3 probe's rule); then the PRAMIN window: its
 * first KiB, the KiB at the BAR VM instance if the window covers it, and a
 * coarse sample of the whole window.
 *
 * Two other modes, each with its own report (docs\decisions\
 * 2026-11-11-gt218-register-probe.md, "Next"):
 *   /fb    C:\V9XDIAG\V9XN5FB.INI: video memory through the VBE
 *          framebuffer, BAR3 + 0, BAR1 + 16 MiB and BAR1 + 0, compared;
 *   /crtc  C:\V9XDIAG\V9XN5CR.INI: the VGA CRTC as the VBE left it;
 *   /fbwrite  C:\V9XDIAG\V9XN5FW.INI: the BAR1 linearity write test
 *          (docs\decisions\2026-11-11-gt218-bar-windows.md, "Next");
 *   /startmove  C:\V9XDIAG\V9XN5SM.INI: the VGA start address written,
 *          held 15 s and restored, to see whether the scanout follows it.
 *
 * /vga reads PDISPLAY's VGA block (619E00h-619FFFh, 6194E8h); /rom copies
 * the shadowed VBIOS at C0000h to C:\V9XDIAG\V9XN5ROM.BIN. No MMIO
 * register is ever written. /startmove and /starthigh write VGA CRTC
 * registers (CR0C/CR0D; CR3F, CR35), hold 15 s and restore them as read
 * (docs\decisions\2026-11-16-gt218-flips-through-vga-crtc.md). Video
 * memory is written only by /fbwrite,
 * 16 dwords at each of five BAR1 offsets past 14 MiB, and restored; /crtc
 * writes only the VGA CRTC index port, and puts it back. Every offset and field is from
 * nouveau (nvkm, Linux master, kept in build\external\nv50\nouveau), not
 * measured; the record of the first run says which held.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_N5MM_PATH        V9X_DIAG_PATH("V9XN5MM.INI")
#define V9X_N5FB_PATH        V9X_DIAG_PATH("V9XN5FB.INI")
#define V9X_N5CR_PATH        V9X_DIAG_PATH("V9XN5CR.INI")
#define V9X_N5SM_PATH        V9X_DIAG_PATH("V9XN5SM.INI")
#define V9X_N5VG_PATH        V9X_DIAG_PATH("V9XN5VG.INI")
#define V9X_N5MM_VXD         "\\\\.\\N5MM.VXD"
#define V9X_N5MM_MAGIC       0x4d4d354eul
#define V9X_N5MM_CAPTURE     1u
#define V9X_N5MM_FB          2u
#define V9X_N5MM_FBWRITE     3u
#define V9X_N5MM_HWSQ        4u
#define V9X_ST_HWSQ_DONE     0x00000800ul
#define V9X_ST_HWSQ_TIMEOUT  0x00001000ul
#define V9X_N5CK_PATH        V9X_DIAG_PATH("V9XN5CK.INI")
#define V9X_N5FW_PATH        V9X_DIAG_PATH("V9XN5FW.INI")
#define V9X_ST_WRITE_DONE    0x00000400ul

/* N5MM.VXD's write test: BAR1 + 0, then five targets past 14 MiB, 16
 * dwords each, each target's pattern A5000000h | window << 16 | byte. */
#define V9X_N5_WR_WINDOWS    6ul
#define V9X_N5_WR_DWORDS     16ul
#define V9X_N5_WR_PATTERN    0xa5000000ul
#define V9X_N5_WR_TAG_MASK   0xff000000ul
#define V9X_N5MM_OFFSET_MAX  256u
#define V9X_N5MM_HEADER      16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        8ul
#define V9X_PCI_DEVICES      32ul
#define V9X_PCI_CONFIG_BYTES 256ul
#define V9X_GT218_ID         0x0a6510deul
/* GeForce 9500 GT (docs\plans\nvidia-g96-geforce9500gt.md, Phase 2): the
 * default pass only; the other modes' offsets and windows are the
 * GT218's and unchecked on a G96. */
#define V9X_G96_ID           0x064010deul
#define V9X_NV50_CHIPSET_GT218 0xa8ul
#define V9X_NV50_CHIPSET_G96   0x96ul

#define V9X_ST_GT218           0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_FB_MAPPED       0x00000200ul

/* N5MM.VXD's framebuffer-window encoding: bit 31, the window in 25:24,
 * the byte in 20:0. Windows in the order /fb reads them, BAR1 last
 * because nothing says what it decodes with no BAR VM. */
#define V9X_N5_FB_FLAG         0x80000000ul
#define V9X_N5_FB_WINDOW_SHIFT 24u
#define V9X_N5_FB_OFFSET_MASK  0x001ffffful
#define V9X_N5_FB_BYTES        0x00200000ul
#define V9X_N5_FB_STEP         (V9X_N5_FB_BYTES / V9X_N5MM_OFFSET_MAX)
#define V9X_N5_FB_LFB          0ul
#define V9X_N5_FB_WINDOWS      4ul

/* /crtc: the VGA ports, read only (index writes and data reads). */
#define V9X_VGA_MISC_READ      ((WORD)0x03ccu)
#define V9X_VGA_CRTC_INDEX     ((WORD)0x03d4u)
#define V9X_VGA_CRTC_DATA      ((WORD)0x03d5u)

/* Registers the decode reads back. */
#define V9X_NV50_PMC_BOOT_0       0x000000ul
#define V9X_NV50_PMC_ENABLE       0x000200ul
#define V9X_NV50_PBUS_UNITS       0x001540ul
#define V9X_NV50_PRAMIN_WINDOW    0x001700ul
#define V9X_NV50_BAR_VM           0x001704ul
#define V9X_NV50_BAR1_OBJECT      0x001708ul
#define V9X_NV50_BAR3_OBJECT      0x00170cul
#define V9X_NV50_PFB_CONFIG_0     0x100200ul
#define V9X_NV50_PFB_MEM_SIZE     0x10020cul
#define V9X_NV50_PFB_MEM_TYPE     0x100714ul
#define V9X_NV50_PSTRAPS          0x101000ul
#define V9X_NV50_PRAMIN_START     0x700000ul
#define V9X_NV50_PRAMIN_BYTES     0x100000ul
/* PBUS ROM access (1850h, nouveau's PCI mirror 0x50): bit 0 set shadows
 * the ROM; clear, the PROM window at BAR0 + 300000h reads the ROM chip
 * (subdev\pci\base.c nvkm_pci_rom_shadow, subdev\bios\shadowrom.c). 128 KiB
 * covers a G9x board's image and its PCIR chain. */
#define V9X_NV50_ROM_ACCESS       0x001850ul
#define V9X_NV50_ROM_SHADOW       0x00000001ul
#define V9X_NV50_PROM             0x300000ul
#define V9X_NV50_PROM_BYTES       0x020000ul

/*
 * Field layouts, all nouveau's. PMC_BOOT_0 chipset 28:20, stepping 7:0.
 * PMC_ENABLE: PFIFO 00000100h, PGRAPH 00201000h, the copy engine
 * 00802000h (subdev\mc\gt215.c, gt215_mc_reset). Memory size: 10020Ch, low
 * byte the bits above 32, the rest the low word (ramnv50.c, nv50_ram_ctor).
 * Memory type: 100714h bits 2:0 (0 DDR1, 1 DDR2 or DDR3 by the BIOS, 2
 * GDDR3, 3 GDDR4, 4 GDDR5). Partitions: 1540h bits 23:16, one bit each.
 * Ranks: 100200h bit 2. PRAMIN window: 1700h holds the video memory
 * address >> 16 (instmem\nv50.c). BAR VM: 1704h holds the instance address
 * >> 12, bit 30 set once valid; 1708h (BAR1) and 170Ch (BAR3) hold bit 31
 * valid and an object offset >> 4 (bar\nv50.c).
 */
#define V9X_NV50_CHIPSET_SHIFT    20u
#define V9X_NV50_CHIPSET_MASK     0x1fful
#define V9X_NV50_STEPPING_MASK    0xfful
#define V9X_NV50_ENABLE_PFIFO     0x00000100ul
#define V9X_NV50_ENABLE_PGRAPH    0x00201000ul
#define V9X_NV50_ENABLE_CE        0x00802000ul
#define V9X_NV50_SIZE_HIGH_MASK   0x000000fful
#define V9X_NV50_SIZE_LOW_MASK    0xffffff00ul
#define V9X_NV50_TYPE_MASK        0x7ul
#define V9X_NV50_PARTS_SHIFT      16u
#define V9X_NV50_PARTS_MASK       0xfful
#define V9X_NV50_RANKS_TWO        0x00000004ul
#define V9X_NV50_WINDOW_SHIFT     16u
#define V9X_NV50_VM_VALID         0x40000000ul
#define V9X_NV50_VM_ADDR_MASK     0x0ffffffful
#define V9X_NV50_VM_SHIFT         12u
#define V9X_NV50_OBJECT_VALID     0x80000000ul
#define V9X_NV50_OBJECT_MASK      0x7ffffffful
#define V9X_NV50_OBJECT_SHIFT     4u

/* PRAMIN samples: a dense KiB, and one word every 4 KiB of the window. */
#define V9X_PRAMIN_DENSE_BYTES    0x400ul
#define V9X_PRAMIN_COARSE_STEP    0x1000ul

/*
 * PDISP's core channel state, as nouveau's method dump reads it
 * (engine\disp\nv50.c, nv50_disp_core_mthd_head): per head method, the
 * register holding it, head 1 at + 540h, and the word 4 bytes on, which the
 * dump reads beside it (the channel's prev = 4).
 */
#define V9X_NV50_HEAD_STRIDE      0x540ul
#define V9X_NV50_HEAD_PREV        0x4ul
#define V9X_NV50_HEAD_OFFSET_REG  0x610a84ul /* method 0860h */

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []
static BYTE v9x_inpb(WORD port);
#pragma aux v9x_inpb = "in al,dx" parm [dx] value [al] modify exact [al]
static void v9x_outpb(WORD port, BYTE value);
#pragma aux v9x_outpb = "out dx,al" parm [dx] [al] modify exact []

struct v9x_n5mm_register {
    DWORD offset;
    const char *name;
};

/* First capture: units that are always present. */
static const struct v9x_n5mm_register v9x_global[] = {
    { 0x000000ul, "PMC_BOOT_0" },
    { 0x000004ul, "PMC_BOOT_1" },
    { 0x000100ul, "PMC_INTR_0" },
    { 0x000140ul, "PMC_INTR_EN_0" },
    { 0x000200ul, "PMC_ENABLE" },
    { 0x001540ul, "PBUS_UNITS" },
    { 0x001700ul, "PBUS_PRAMIN_WINDOW" },
    { 0x001704ul, "PBUS_BAR_VM" },
    { 0x001708ul, "PBUS_BAR1_OBJECT" },
    { 0x00170cul, "PBUS_BAR3_OBJECT" },
    { 0x001850ul, "PBUS_ROM_ACCESS" },
    { 0x009400ul, "PTIMER_TIME_0" },
    { 0x009404ul, "PTIMER_TIME_1" },
    { 0x100200ul, "PFB_CONFIG_0" },
    { 0x100204ul, "PFB_CONFIG_1" },
    { 0x100208ul, "PFB_208" },
    { 0x10020cul, "PFB_MEM_SIZE" },
    { 0x100714ul, "PFB_MEM_TYPE" },
    { 0x101000ul, "PSTRAPS" },
    { 0x610020ul, "PDISP_INTR_0" },
    { 0x610024ul, "PDISP_INTR_1" },
    { 0x610028ul, "PDISP_INTR_EN" },
    { 0x610030ul, "PDISP_SUPERVISOR" },
    { 0x610184ul, "PDISP_CAPS" },
    { 0x610200ul, "PDISP_CORE_CTRL" },
    { 0x610bb8ul, "PDISP_CORE_M0084" },
    { 0x610b9cul, "PDISP_CORE_M0088" },
    { 0x610b58ul, "PDISP_DAC0_M0400" },
    { 0x610b70ul, "PDISP_SOR0_M0600" },
    { 0x610b78ul, "PDISP_SOR1_M0600" }
};

/* The head method table, method and register for head 0. */
static const DWORD v9x_head[][2] = {
    { 0x0800ul, 0x610ad8ul }, { 0x0804ul, 0x610ad0ul },
    { 0x0808ul, 0x610a48ul }, { 0x080cul, 0x610a78ul },
    { 0x0810ul, 0x610ac0ul }, { 0x0814ul, 0x610af8ul },
    { 0x0818ul, 0x610b00ul }, { 0x081cul, 0x610ae8ul },
    { 0x0820ul, 0x610af0ul }, { 0x0824ul, 0x610b08ul },
    { 0x0828ul, 0x610b10ul }, { 0x082cul, 0x610a68ul },
    { 0x0830ul, 0x610a60ul }, { 0x0838ul, 0x610a40ul },
    { 0x0840ul, 0x610a24ul }, { 0x0844ul, 0x610a2cul },
    { 0x0848ul, 0x610aa8ul }, { 0x084cul, 0x610ab0ul },
    { 0x0860ul, 0x610a84ul }, { 0x0864ul, 0x610a90ul },
    { 0x0868ul, 0x610b18ul }, { 0x086cul, 0x610b20ul },
    { 0x0870ul, 0x610ac8ul }, { 0x0874ul, 0x610a38ul },
    { 0x0880ul, 0x610a58ul }, { 0x0884ul, 0x610a9cul },
    { 0x08a0ul, 0x610a70ul }, { 0x08a4ul, 0x610a50ul },
    { 0x08a8ul, 0x610ae0ul }, { 0x08c0ul, 0x610b28ul },
    { 0x08c4ul, 0x610b30ul }, { 0x08c8ul, 0x610b40ul },
    { 0x08d4ul, 0x610b38ul }, { 0x08d8ul, 0x610b48ul },
    { 0x08dcul, 0x610b50ul }, { 0x0900ul, 0x610a18ul },
    { 0x0904ul, 0x610ab8ul }
};

static const struct v9x_n5mm_register v9x_pfifo[] = {
    { 0x002100ul, "PFIFO_INTR" },
    { 0x002140ul, "PFIFO_INTR_EN" },
    { 0x002500ul, "PFIFO_CACHES" },
    { 0x00250cul, "PFIFO_250C" },
    { 0x002600ul, "PFIFO_CHAN_0" },
    { 0x002604ul, "PFIFO_CHAN_1" },
    { 0x002608ul, "PFIFO_CHAN_2" },
    { 0x00260cul, "PFIFO_CHAN_3" },
    { 0x002610ul, "PFIFO_CHAN_4" },
    { 0x002614ul, "PFIFO_CHAN_5" },
    { 0x002618ul, "PFIFO_CHAN_6" },
    { 0x00261cul, "PFIFO_CHAN_7" },
    { 0x003200ul, "PFIFO_CACHE1_PUSH" },
    { 0x003250ul, "PFIFO_CACHE1_PULL" },
    { 0x0032ecul, "PFIFO_32EC" },
    { 0x0032f0ul, "PFIFO_32F0" },
    { 0x0032f4ul, "PFIFO_32F4" },
    { 0x0032f8ul, "PFIFO_32F8" },
    { 0x0032fcul, "PFIFO_32FC" }
};

static const struct v9x_n5mm_register v9x_pgraph[] = {
    { 0x400100ul, "PGRAPH_INTR" },
    { 0x400108ul, "PGRAPH_TRAP" },
    { 0x40032cul, "PGRAPH_CTX_INSTANCE" }
};

/* The G96's clock registers, every one nouveau's nv50_clk_read reads for
 * chipset 96h (subdev\clk\nv50.c): the source select, the PLL control and
 * coefficient pairs, the reference PLLs behind them, the divider word.
 * The GT218 clocks through a different unit (clk\gt215.c), so this list is
 * read on the G96 only. The core, shader and memory clocks are decoded on
 * the host from these words. */
static const struct v9x_n5mm_register v9x_clock[] = {
    { 0x004000ul, "PCLOCK_4000" },
    { 0x004004ul, "PCLOCK_4004" },
    { 0x004008ul, "PCLOCK_MPLL_CTRL" },
    { 0x00400cul, "PCLOCK_MPLL_COEF" },
    { 0x004010ul, "PCLOCK_4010" },
    { 0x004014ul, "PCLOCK_4014" },
    { 0x004018ul, "PCLOCK_4018" },
    { 0x00401cul, "PCLOCK_401C" },
    { 0x004020ul, "PCLOCK_SPLL_CTRL" },
    { 0x004024ul, "PCLOCK_SPLL_COEF" },
    { 0x004028ul, "PCLOCK_NVPLL_CTRL" },
    { 0x00402cul, "PCLOCK_NVPLL_COEF" },
    { 0x004030ul, "PCLOCK_VDEC_CTRL" },
    { 0x004034ul, "PCLOCK_VDEC_COEF" },
    { 0x004038ul, "PCLOCK_4038" },
    { 0x00403cul, "PCLOCK_403C" },
    { 0x004800ul, "PCLOCK_DIV_G9X" },
    { 0x00c040ul, "PCLOCK_SOURCE" },
    { 0x00c050ul, "PCLOCK_REF_SELECT" },
    { 0x00e81cul, "PCLOCK_REF0_COEF" },
    { 0x00e820ul, "PCLOCK_REF0_820" },
    { 0x00e824ul, "PCLOCK_REF0_POST" },
    { 0x00e844ul, "PCLOCK_REF1_COEF" },
    { 0x00e848ul, "PCLOCK_REF1_848" },
    { 0x00e84cul, "PCLOCK_REF1_POST" },
    /* The GPIO lines, input sense in bit 2 of each line's nibble: the
     * VBIOS voltage table's VID lines are among them (gpio\nv50.c). */
    { 0x00e104ul, "PGPIO_LINES_0_7" },
    { 0x00e108ul, "PGPIO_LINES_8_15" },
    { 0x00e280ul, "PGPIO_LINES_16_23" },
    { 0x00e284ul, "PGPIO_LINES_24_31" }
};

/*
 * /reclock (docs\plans\nvidia-g96-reclock.md). The script is nouveau's
 * nv50_clk_calc for a core and shader change on chipset 96h, encoded by
 * nouveau's hwsq rules (subdev\bus\hwsq.c), and run by N5MM.VXD's
 * N5_DIOC_HWSQ. The shader always goes through SPLL (nv50_clk_calc's
 * second branch), so the POST state can be put back exactly.
 *
 * Registers and fields (subdev\clk\nv50.c): PFIFO block 2504h; source
 * select C040h, bits 1:0 core source (3 = NVPLL), 5:4 shader source (3 =
 * SPLL), 7 shader on hclk while unhooked, 20 the GT200 core bypass; PLL
 * control 4028h/4020h, bit 31 enable, 18:16 P (and 21:19 its copy); PLL
 * coefficients 402Ch/4024h, 15:8 N, 7:0 M.
 */
#define V9X_NV50_PFIFO_BLOCK      0x002504ul
#define V9X_NV50_CLK_SOURCE       0x00c040ul
#define V9X_NV50_CLK_REF_SELECT   0x00c050ul
#define V9X_NV50_SPLL_CTRL        0x004020ul
#define V9X_NV50_SPLL_COEF        0x004024ul
#define V9X_NV50_NVPLL_CTRL       0x004028ul
#define V9X_NV50_NVPLL_COEF       0x00402cul
#define V9X_NV50_GPIO_0_7         0x00e104ul
#define V9X_NV50_PLL_ENABLE       0x80000000ul
#define V9X_NV50_PLL_CTRL_MASK    0xc03f0100ul
#define V9X_NV50_PLL_P_SHIFT      16u
#define V9X_NV50_PLL_P_COPY_SHIFT 19u
#define V9X_NV50_PLL_P_MASK       0x7ul
#define V9X_NV50_PLL_COEF_MASK    0x0000fffful
#define V9X_NV50_SRC_UNHOOK_MASK  0x000000b3ul
#define V9X_NV50_SRC_UNHOOK       0x00000081ul
#define V9X_NV50_SRC_HOOK_MASK    0x00100033ul
#define V9X_NV50_SRC_HOOK         0x00000033ul
/* C050h fields saying both PLLs take the 100 MHz PCIe reference (nv50.c
 * read_pll_src, chipset 94h-98h): bits 1:0 SPLL, 12:11 NVPLL, 2 = href. */
#define V9X_NV50_REF_MASK         0x00001803ul
#define V9X_NV50_REF_BOTH_HREF    0x00001002ul
/* hwsq flag 10h is the FB enable; wait flag 0 its state (nv50_clk_calc);
 * 2504h = 1 blocks PFIFO, then 8 us. */
#define V9X_HWSQ_FLAG_FB          0x10u
#define V9X_HWSQ_WAIT_FB          0x00u
#define V9X_HWSQ_FIFO_SETTLE_NS   8000ul
#define V9X_HWSQ_BYTES            512u
#define V9X_HWSQ_FILL             0x7fu

/* This board's VBIOS (62.94.2C.00.91, docs\decisions\
 * 2026-11-28-g96-clocks-measured.md): the voltage table's one VID bit is
 * DCB GPIO function 04h on line 5, entry 200004E5h (nouveau dcb gpio v4:
 * log[1] = bits 30:29 = 1, so the VID bit is the line's sense as read);
 * VID 0 is 1.00 V, level 3's voltage. Sense: bit 2 of the line's nibble
 * (gpio\nv50.c). */
#define V9X_G96_VID_LINE_SHIFT    20u
#define V9X_G96_VID_SENSE         0x4ul
#define V9X_G96_VID_LEVEL3        0ul

/* The clocks: VBIOS perf level 3 (core 550, shader 1400) and POST (400,
 * 800), from the 100 MHz reference, inside the PLL limits table's ranges
 * (NVPLL VCO 500-1000, input 14-75; SPLL VCO 1000-2000, input to 100). */
struct v9x_g96_clocks {
    DWORD core_n;
    DWORD core_m;
    DWORD core_p;
    DWORD shader_n;
    DWORD shader_m;
    DWORD shader_p;
};

static const struct v9x_g96_clocks v9x_g96_level3 = { 11ul, 2ul, 0ul,
                                                      14ul, 1ul, 0ul };
static const struct v9x_g96_clocks v9x_g96_post = { 16ul, 2ul, 1ul,
                                                    16ul, 1ul, 1ul };

struct v9x_hwsq {
    BYTE code[V9X_HWSQ_BYTES];
    DWORD size;
    DWORD addr;
    DWORD data;
};

static struct v9x_hwsq v9x_sq;

static void v9x_sq_bytes(const BYTE *bytes, DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        if (v9x_sq.size < V9X_HWSQ_BYTES) {
            v9x_sq.code[v9x_sq.size] = bytes[index];
            ++v9x_sq.size;
        }
    }
}

/* nvkm_hwsq_wr32: a data opcode when the value changes (E2h whole, 42h
 * low half), then an address opcode (E0h whole, 40h low half). */
static void v9x_sq_wr32(DWORD addr, DWORD data)
{
    BYTE op[5];

    if (data != v9x_sq.data) {
        if ((data & 0xffff0000ul) != (v9x_sq.data & 0xffff0000ul)) {
            op[0] = 0xe2u;
            op[1] = (BYTE)data;
            op[2] = (BYTE)(data >> 8);
            op[3] = (BYTE)(data >> 16);
            op[4] = (BYTE)(data >> 24);
            v9x_sq_bytes(op, 5ul);
        } else {
            op[0] = 0x42u;
            op[1] = (BYTE)data;
            op[2] = (BYTE)(data >> 8);
            v9x_sq_bytes(op, 3ul);
        }
    }
    if ((addr & 0xffff0000ul) != (v9x_sq.addr & 0xffff0000ul)) {
        op[0] = 0xe0u;
        op[1] = (BYTE)addr;
        op[2] = (BYTE)(addr >> 8);
        op[3] = (BYTE)(addr >> 16);
        op[4] = (BYTE)(addr >> 24);
        v9x_sq_bytes(op, 5ul);
    } else {
        op[0] = 0x40u;
        op[1] = (BYTE)addr;
        op[2] = (BYTE)(addr >> 8);
        v9x_sq_bytes(op, 3ul);
    }
    v9x_sq.addr = addr;
    v9x_sq.data = data;
}

/* hwsq_mask: written only when the value changes. */
static void v9x_sq_mask(DWORD addr, DWORD *cache, DWORD mask, DWORD data)
{
    DWORD value;

    value = (*cache & ~mask) | data;
    if (value != *cache) {
        v9x_sq_wr32(addr, value);
        *cache = value;
    }
}

/* nvkm_hwsq_setf: 80h + flag, + 20h for 0, + 40h for 1. */
static void v9x_sq_setf(BYTE flag, DWORD on)
{
    BYTE op;

    op = (BYTE)(0x80u + flag + 0x20u + (on != 0ul ? 0x20u : 0u));
    v9x_sq_bytes(&op, 1ul);
}

static void v9x_sq_wait(BYTE flag, BYTE data)
{
    BYTE op[3];

    op[0] = 0x5fu;
    op[1] = flag;
    op[2] = data;
    v9x_sq_bytes(op, 3ul);
}

/* nvkm_hwsq_nsec: microseconds as a 2-bit count shifted by 2 bits a step. */
static void v9x_sq_nsec(DWORD nsec)
{
    DWORD usec;
    DWORD shift;
    BYTE op;

    usec = nsec / 1000ul;
    shift = 0ul;
    while ((usec & ~3ul) != 0ul) {
        usec >>= 2;
        ++shift;
    }
    op = (BYTE)((shift << 2) | usec);
    v9x_sq_bytes(&op, 1ul);
}

/* nv50_clk_calc's core and shader part, the shader on SPLL, from the
 * registers' current values. */
static void v9x_sq_build(const struct v9x_g96_clocks *to, DWORD mast,
                         DWORD nvpll_ctrl, DWORD nvpll_coef,
                         DWORD spll_ctrl, DWORD spll_coef)
{
    DWORD index;

    /* No C runtime here: FillMemory is memset. */
    for (index = 0ul; index < V9X_HWSQ_BYTES; ++index) {
        v9x_sq.code[index] = V9X_HWSQ_FILL;
    }
    v9x_sq.size = 0ul;
    v9x_sq.addr = 0xfffffffful;
    v9x_sq.data = 0xfffffffful;

    v9x_sq_wr32(V9X_NV50_PFIFO_BLOCK, 1ul);
    v9x_sq_nsec(V9X_HWSQ_FIFO_SETTLE_NS);
    v9x_sq_setf(V9X_HWSQ_FLAG_FB, 0ul);
    v9x_sq_wait(V9X_HWSQ_WAIT_FB, 1u);

    v9x_sq_mask(V9X_NV50_CLK_SOURCE, &mast, V9X_NV50_SRC_UNHOOK_MASK,
                V9X_NV50_SRC_UNHOOK);
    v9x_sq_mask(V9X_NV50_NVPLL_CTRL, &nvpll_ctrl, V9X_NV50_PLL_CTRL_MASK,
                V9X_NV50_PLL_ENABLE |
                (to->core_p << V9X_NV50_PLL_P_COPY_SHIFT) |
                (to->core_p << V9X_NV50_PLL_P_SHIFT));
    v9x_sq_mask(V9X_NV50_NVPLL_COEF, &nvpll_coef, V9X_NV50_PLL_COEF_MASK,
                (to->core_n << 8) | to->core_m);
    v9x_sq_mask(V9X_NV50_SPLL_CTRL, &spll_ctrl, V9X_NV50_PLL_CTRL_MASK,
                V9X_NV50_PLL_ENABLE |
                (to->shader_p << V9X_NV50_PLL_P_COPY_SHIFT) |
                (to->shader_p << V9X_NV50_PLL_P_SHIFT));
    v9x_sq_mask(V9X_NV50_SPLL_COEF, &spll_coef, V9X_NV50_PLL_COEF_MASK,
                (to->shader_n << 8) | to->shader_m);
    v9x_sq_mask(V9X_NV50_CLK_SOURCE, &mast, V9X_NV50_SRC_HOOK_MASK,
                V9X_NV50_SRC_HOOK);

    v9x_sq_setf(V9X_HWSQ_FLAG_FB, 1ul);
    v9x_sq_wait(V9X_HWSQ_WAIT_FB, 0u);
    v9x_sq_wr32(V9X_NV50_PFIFO_BLOCK, 0ul);
}

static const struct v9x_n5mm_register v9x_copy[] = {
    { 0x104040ul, "PCOPY_FALCON_SSTA" },
    { 0x104100ul, "PCOPY_FALCON_CPUCTL" },
    { 0x104104ul, "PCOPY_FALCON_BOOTVEC" },
    { 0x104108ul, "PCOPY_FALCON_HWCFG" },
    { 0x10410cul, "PCOPY_FALCON_10C" }
};

struct v9x_n5mm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_N5MM_OFFSET_MAX];
};

struct v9x_n5mm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD accepted;
    DWORD boot0;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD bar1;
    DWORD class_revision;
    DWORD bar3;
    DWORD reserved[4];
    DWORD offsets[V9X_N5MM_OFFSET_MAX];
    DWORD first[V9X_N5MM_OFFSET_MAX];
    DWORD second[V9X_N5MM_OFFSET_MAX];
};

/* The header must be the VxD's N5_HEADER_DWORDS long. */
typedef char v9x_n5mm_header_matches[
    offsetof(struct v9x_n5mm_result, offsets) ==
    sizeof(DWORD) * V9X_N5MM_HEADER ? 1 : -1];

struct v9x_n5mm_value {
    DWORD offset;
    DWORD value;
};

static struct v9x_n5mm_request v9x_request;
static struct v9x_n5mm_result v9x_result;
static struct v9x_n5mm_value v9x_values[V9X_N5MM_OFFSET_MAX * 2u];
static DWORD v9x_value_count;

/* The report this run writes: V9XN5MM.INI, or /fb's and /crtc's own. */
static const char *v9x_path = V9X_N5MM_PATH;

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(V9X_PCI_ADDRESS);
    v9x_outpd(V9X_PCI_ADDRESS, device | (offset & 0xfcul));
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

static void v9x_decimal(char *text, DWORD value)
{
    char digits[11];
    int count;
    int at;

    count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    at = 0;
    while (count != 0) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, v9x_path);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, v9x_path);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, v9x_path);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset,
                           int digits)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, digits);
}

static void v9x_request_add(DWORD offset)
{
    if (v9x_request.count < V9X_N5MM_OFFSET_MAX) {
        v9x_request.offsets[v9x_request.count++] = offset;
    }
}

static void v9x_name(DWORD offset, const char *name)
{
    char key[24];

    v9x_offset_key(key, "Name.", offset, 6);
    v9x_text("Mmio", key, name);
}

static void v9x_request_list(const struct v9x_n5mm_register *list,
                             DWORD count)
{
    DWORD index;

    v9x_request.count = 0ul;
    for (index = 0ul; index < count; ++index) {
        v9x_request_add(list[index].offset);
        v9x_name(list[index].offset, list[index].name);
    }
}

/* Both heads' method registers and the word beside each. */
static void v9x_request_heads(void)
{
    char name[24];
    DWORD head;
    DWORD index;
    DWORD offset;

    v9x_request.count = 0ul;
    for (head = 0ul; head < 2ul; ++head) {
        for (index = 0ul; index < sizeof(v9x_head) / sizeof(v9x_head[0]);
             ++index) {
            offset = v9x_head[index][1] + head * V9X_NV50_HEAD_STRIDE;
            lstrcpyA(name, head == 0ul ? "HEAD0_M" : "HEAD1_M");
            v9x_hex(name + lstrlenA(name), v9x_head[index][0], 4);
            v9x_request_add(offset);
            v9x_name(offset, name);
            lstrcatA(name, "_PREV");
            v9x_request_add(offset + V9X_NV50_HEAD_PREV);
            v9x_name(offset + V9X_NV50_HEAD_PREV, name);
        }
    }
}

/* PRAMIN window offsets from start, bytes long, every step bytes. */
static void v9x_request_pramin(DWORD start, DWORD bytes, DWORD step)
{
    DWORD at;

    v9x_request.count = 0ul;
    for (at = 0ul; at < bytes && start + at < V9X_NV50_PRAMIN_BYTES;
         at += step) {
        v9x_request_add(V9X_NV50_PRAMIN_START + start + at);
    }
}

/* One DIOC call of v9x_request. Returns nonzero on a snapshot. */
static int v9x_call(DWORD code)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_N5MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, code, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N5MM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_SNAPSHOT_TAKEN) != 0ul;
}

static int v9x_capture(void)
{
    return v9x_call(V9X_N5MM_CAPTURE);
}

static void v9x_remember(DWORD offset, DWORD value)
{
    if (v9x_value_count < sizeof(v9x_values) / sizeof(v9x_values[0])) {
        v9x_values[v9x_value_count].offset = offset;
        v9x_values[v9x_value_count].value = value;
        ++v9x_value_count;
    }
}

static DWORD v9x_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_value_count; ++index) {
        if (v9x_values[index].offset == offset) {
            return v9x_values[index].value;
        }
    }
    return 0ul;
}

static void v9x_pair(char *text, DWORD index)
{
    v9x_hex(text, v9x_result.first[index], 8);
    if (v9x_result.second[index] != v9x_result.first[index]) {
        text[8] = ',';
        v9x_hex(text + 9, v9x_result.second[index], 8);
    }
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        v9x_offset_key(key, "R", v9x_result.offsets[index], 6);
        v9x_pair(text, index);
        v9x_text("Mmio", key, text);
        v9x_remember(v9x_result.offsets[index], v9x_result.first[index]);
    }
}

/* Non-zero words only, keyed by their offset in the window. */
static void v9x_publish_pramin(void)
{
    char key[24];
    char text[24];
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        if (v9x_result.first[index] == 0ul &&
            v9x_result.second[index] == 0ul) {
            continue;
        }
        v9x_offset_key(key, "P",
                       v9x_result.offsets[index] - V9X_NV50_PRAMIN_START, 5);
        v9x_pair(text, index);
        v9x_text("Pramin", key, text);
    }
}

/* The first GT218 or G96 on buses 0-7, function 0; 0 if none. */
static DWORD v9x_find_gt218(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;
    DWORD id;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            id = v9x_config_read(config, 0ul);
            if (id == V9X_GT218_ID || id == V9X_G96_ID) {
                return config;
            }
        }
    }
    return 0ul;
}

static void v9x_publish_config(DWORD config)
{
    char key[8];
    DWORD offset;

    for (offset = 0ul; offset < V9X_PCI_CONFIG_BYTES; offset += 4ul) {
        v9x_offset_key(key, "C", offset, 2);
        v9x_key("Config", key, v9x_config_read(config, offset));
    }
}

static DWORD v9x_bits_set(DWORD value)
{
    DWORD count = 0ul;

    while (value != 0ul) {
        count += value & 1ul;
        value >>= 1;
    }
    return count;
}

static void v9x_decode_global(void)
{
    static const char *const types[] = {
        "ddr1", "ddr2-or-ddr3", "gddr3", "gddr4", "gddr5",
        "unknown-5", "unknown-6", "unknown-7"
    };
    DWORD value;

    value = v9x_value(V9X_NV50_PMC_BOOT_0);
    v9x_key("Decode", "Chipset",
            (value >> V9X_NV50_CHIPSET_SHIFT) & V9X_NV50_CHIPSET_MASK);
    v9x_key("Decode", "Stepping", value & V9X_NV50_STEPPING_MASK);

    value = v9x_value(V9X_NV50_PMC_ENABLE);
    v9x_text("Decode", "PfifoEnabled",
             (value & V9X_NV50_ENABLE_PFIFO) == V9X_NV50_ENABLE_PFIFO ?
             "yes" : "no");
    v9x_text("Decode", "PgraphEnabled",
             (value & V9X_NV50_ENABLE_PGRAPH) == V9X_NV50_ENABLE_PGRAPH ?
             "yes" : "no");
    v9x_text("Decode", "CopyEnabled",
             (value & V9X_NV50_ENABLE_CE) == V9X_NV50_ENABLE_CE ?
             "yes" : "no");

    /* Memory size: bits 39:32 in the low byte. Bytes as two keys, and
     * MiB, which is what a 32-bit driver can use. */
    value = v9x_value(V9X_NV50_PFB_MEM_SIZE);
    v9x_key("Decode", "MemoryBytesHigh", value & V9X_NV50_SIZE_HIGH_MASK);
    v9x_key("Decode", "MemoryBytesLow", value & V9X_NV50_SIZE_LOW_MASK);
    v9x_number("Decode", "MemoryMiB",
               ((value & V9X_NV50_SIZE_HIGH_MASK) << 12) |
               ((value & V9X_NV50_SIZE_LOW_MASK) >> 20));
    v9x_text("Decode", "MemoryType",
             types[v9x_value(V9X_NV50_PFB_MEM_TYPE) & V9X_NV50_TYPE_MASK]);
    v9x_number("Decode", "MemoryPartitions",
               v9x_bits_set((v9x_value(V9X_NV50_PBUS_UNITS) >>
                             V9X_NV50_PARTS_SHIFT) & V9X_NV50_PARTS_MASK));
    v9x_number("Decode", "MemoryRanks",
               (v9x_value(V9X_NV50_PFB_CONFIG_0) & V9X_NV50_RANKS_TWO) != 0ul
               ? 2ul : 1ul);
    v9x_key("Decode", "Straps", v9x_value(V9X_NV50_PSTRAPS));

    v9x_key("Decode", "PraminWindowBase",
            v9x_value(V9X_NV50_PRAMIN_WINDOW) << V9X_NV50_WINDOW_SHIFT);
    value = v9x_value(V9X_NV50_BAR_VM);
    v9x_text("Decode", "BarVmValid",
             (value & V9X_NV50_VM_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "BarVmInstance",
            (value & V9X_NV50_VM_ADDR_MASK) << V9X_NV50_VM_SHIFT);
    value = v9x_value(V9X_NV50_BAR1_OBJECT);
    v9x_text("Decode", "Bar1ObjectValid",
             (value & V9X_NV50_OBJECT_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "Bar1ObjectOffset",
            (value & V9X_NV50_OBJECT_MASK) << V9X_NV50_OBJECT_SHIFT);
    value = v9x_value(V9X_NV50_BAR3_OBJECT);
    v9x_text("Decode", "Bar3ObjectValid",
             (value & V9X_NV50_OBJECT_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "Bar3ObjectOffset",
            (value & V9X_NV50_OBJECT_MASK) << V9X_NV50_OBJECT_SHIFT);
}

/* Each head's surface offset method, read as the address >> 8. That shift
 * is assumed, not taken from a fetched source; the raw words are in
 * [Mmio] for the record to check against VBE's framebuffer. */
static void v9x_decode_heads(void)
{
    v9x_key("Decode", "Head0SurfaceOffset",
            v9x_value(V9X_NV50_HEAD_OFFSET_REG) << 8);
    v9x_key("Decode", "Head1SurfaceOffset",
            v9x_value(V9X_NV50_HEAD_OFFSET_REG + V9X_NV50_HEAD_STRIDE) << 8);
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, v9x_path);
    ExitProcess(code);
}

static void v9x_optional(const struct v9x_n5mm_register *list, DWORD count,
                         DWORD enable, DWORD mask, const char *report_key,
                         const char *failure)
{
    if ((enable & mask) != mask) {
        v9x_text("Report", report_key, "skipped-disabled");
        return;
    }
    v9x_request_list(list, count);
    if (!v9x_capture()) {
        v9x_finish(failure, 3u);
    }
    v9x_publish_mmio();
    v9x_text("Report", report_key, "taken");
}

static int v9x_switch_given(const char *switch_text)
{
    const char *line;
    int length;

    line = GetCommandLineA();
    length = lstrlenA(switch_text);
    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            return 1;
        }
        ++line;
    }
    return 0;
}

/*
 * /fb: the same 256 offsets, one every 8 KiB of 2 MiB, through each of the
 * four windows: the VBE framebuffer (BAR3 + 16 MiB), BAR3 + 0, BAR1 +
 * 16 MiB, BAR1 + 0, in that order. The report is flushed after each, so a
 * window that hangs the machine leaves the ones before it on disk. Per
 * window: every word (W<n>.<offset>), and counts of words equal to the
 * framebuffer's, zero, all ones, and moved between the two reads.
 */
static DWORD v9x_fb_lfb[V9X_N5MM_OFFSET_MAX];

static void v9x_fb_window(DWORD window)
{
    char section[8];
    char key[24];
    char text[24];
    DWORD index;
    DWORD offset;
    DWORD same = 0ul;
    DWORD zero = 0ul;
    DWORD ones = 0ul;
    DWORD moved = 0ul;

    v9x_request.count = 0ul;
    for (offset = 0ul; offset < V9X_N5_FB_BYTES; offset += V9X_N5_FB_STEP) {
        v9x_request_add(V9X_N5_FB_FLAG |
                        (window << V9X_N5_FB_WINDOW_SHIFT) | offset);
    }
    lstrcpyA(section, "Fb");
    v9x_hex(section + 2, window, 1);
    if (!v9x_call(V9X_N5MM_FB)) {
        v9x_key(section, "Status", v9x_result.status);
        v9x_finish("FB-CAPTURE-FAILED", 5u);
    }
    v9x_key(section, "Status", v9x_result.status);
    v9x_key("Report", "Bar1", v9x_result.bar1);
    v9x_key("Report", "Bar3", v9x_result.bar3);
    for (index = 0ul; index < v9x_result.accepted; ++index) {
        offset = v9x_result.offsets[index] & V9X_N5_FB_OFFSET_MASK;
        v9x_offset_key(key, "W", offset, 6);
        v9x_pair(text, index);
        v9x_text(section, key, text);
        if (window == V9X_N5_FB_LFB) {
            v9x_fb_lfb[index] = v9x_result.first[index];
        }
        if (v9x_result.first[index] == v9x_fb_lfb[index]) {
            ++same;
        }
        if (v9x_result.first[index] == 0ul) {
            ++zero;
        }
        if (v9x_result.first[index] == 0xfffffffful) {
            ++ones;
        }
        if (v9x_result.second[index] != v9x_result.first[index]) {
            ++moved;
        }
    }
    v9x_number(section, "SameAsFramebuffer", same);
    v9x_number(section, "Zero", zero);
    v9x_number(section, "AllOnes", ones);
    v9x_number(section, "Moved", moved);
    WritePrivateProfileStringA(0, 0, 0, v9x_path);
}

/*
 * /fbwrite: is BAR1 linear with no BAR VM? See N5_DIOC_FBWRITE in
 * nv50_mmio_probe.asm. Per window: Patterned (dwords that read back as
 * their own window's pattern), Foreign (dwords carrying another window's
 * pattern: an alias), Restored (dwords equal to the saved word after the
 * restore), and every saved, patterned and restored word.
 */
static void v9x_fb_write(void)
{
    static const DWORD targets[] = {
        0x00000000ul, 0x01000000ul, 0x02000000ul,
        0x04000000ul, 0x08000000ul, 0x0ffff000ul
    };
    char section[8];
    char key[24];
    char text[32];
    DWORD window;
    DWORD dword;
    DWORD at;
    DWORD own;
    DWORD word;
    DWORD patterned;
    DWORD foreign;
    DWORD restored;
    DWORD all_ok = 1ul;

    v9x_request.count = 0ul;
    if (!v9x_call(V9X_N5MM_FBWRITE) ||
        (v9x_result.status & V9X_ST_WRITE_DONE) == 0ul) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_finish("WRITE-NOT-RUN", 6u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "Bar1", v9x_result.bar1);
    for (window = 0ul; window < V9X_N5_WR_WINDOWS; ++window) {
        lstrcpyA(section, "Wr");
        v9x_hex(section + 2, window, 1);
        v9x_key(section, "Bar1Offset", targets[window]);
        patterned = 0ul;
        foreign = 0ul;
        restored = 0ul;
        for (dword = 0ul; dword < V9X_N5_WR_DWORDS; ++dword) {
            at = window * V9X_N5_WR_DWORDS + dword;
            own = V9X_N5_WR_PATTERN | (window << 16) | (dword * 4ul);
            word = v9x_result.first[at];
            if (window != 0ul && word == own) {
                ++patterned;
            } else if ((word & V9X_N5_WR_TAG_MASK) == V9X_N5_WR_PATTERN &&
                       word != v9x_result.offsets[at]) {
                ++foreign;
            }
            if (v9x_result.second[at] == v9x_result.offsets[at]) {
                ++restored;
            }
            v9x_offset_key(key, "D", dword * 4ul, 2);
            v9x_hex(text, v9x_result.offsets[at], 8);
            text[8] = ',';
            v9x_hex(text + 9, word, 8);
            text[17] = ',';
            v9x_hex(text + 18, v9x_result.second[at], 8);
            v9x_text(section, key, text);
        }
        v9x_number(section, "Patterned", patterned);
        v9x_number(section, "Foreign", foreign);
        v9x_number(section, "Restored", restored);
        if (foreign != 0ul || restored != V9X_N5_WR_DWORDS ||
            (window != 0ul && patterned != V9X_N5_WR_DWORDS)) {
            all_ok = 0ul;
        }
    }
    v9x_text("Decode", "Bar1Linear", all_ok ? "consistent" : "no");
}

static BYTE v9x_crtc_read(BYTE index)
{
    v9x_outpb(V9X_VGA_CRTC_INDEX, index);
    return v9x_inpb(V9X_VGA_CRTC_DATA);
}

/*
 * /crtc: the VGA CRTC as the VBE left it, read without unlocking the
 * extended set: the start address (0C/0D), offset (13), and the registers
 * NV4 and later use for the start's high bits, the row offset's high bits
 * and pixel depth (19, 1A, 25, 28, 2D), plus the extended-lock index 1F
 * (xf86-video-nv's names; whether NV50 honours them is what this reads).
 * Locked extended registers may read as zero or as the lock.
 */
static void v9x_crtc_probe(void)
{
    static const BYTE indexes[] = {
        0x01u, 0x0cu, 0x0du, 0x12u, 0x13u, 0x19u, 0x1au, 0x1fu,
        0x25u, 0x28u, 0x2du
    };
    char key[8];
    DWORD index;
    BYTE misc;
    BYTE saved;

    misc = v9x_inpb(V9X_VGA_MISC_READ);
    v9x_key("Crtc", "Misc", misc);
    if ((misc & 0x01u) == 0u) {
        v9x_text("Crtc", "Result", "mono-crtc-not-read");
        return;
    }
    /* The index register is put back as found. */
    saved = v9x_inpb(V9X_VGA_CRTC_INDEX);
    for (index = 0ul; index < sizeof(indexes); ++index) {
        lstrcpyA(key, "CR");
        v9x_hex(key + 2, indexes[index], 2);
        v9x_key("Crtc", key, v9x_crtc_read(indexes[index]));
    }
    v9x_outpb(V9X_VGA_CRTC_INDEX, saved);
    v9x_key("Crtc", "StartWords",
            (DWORD)v9x_crtc_read(0x0du) |
            ((DWORD)v9x_crtc_read(0x0cu) << 8));
    v9x_outpb(V9X_VGA_CRTC_INDEX, saved);
    v9x_text("Crtc", "Result", "taken");
}

/*
 * /startmove: does this head still scan out from the VGA start address in
 * the mode the VBE set? CR0C/CR0D are written to V9X_START_MOVE_WORDS, held
 * for V9X_START_MOVE_HOLD_MS so the picture can be captured (OBS, from the
 * host), then put back as found. The probe's only register write; standard
 * VGA, no extended index touched. A picture that shifts says the flip can
 * be a start-address write; one that does not says the scanout belongs to
 * the EVO head (docs\plans\nvidia-gt218.md, Phase 5).
 */
#define V9X_START_MOVE_WORDS   0x2000u
#define V9X_START_MOVE_HOLD_MS 15000ul

static void v9x_start_move(void)
{
    BYTE misc;
    BYTE saved_index;
    BYTE high;
    BYTE low;

    misc = v9x_inpb(V9X_VGA_MISC_READ);
    v9x_key("Start", "Misc", misc);
    if ((misc & 0x01u) == 0u) {
        v9x_text("Start", "Result", "mono-crtc-not-written");
        return;
    }
    saved_index = v9x_inpb(V9X_VGA_CRTC_INDEX);
    high = v9x_crtc_read(0x0cu);
    low = v9x_crtc_read(0x0du);
    v9x_key("Start", "Before", ((DWORD)high << 8) | (DWORD)low);
    v9x_key("Start", "Written", (DWORD)V9X_START_MOVE_WORDS);
    v9x_text("Start", "Result", "holding");
    WritePrivateProfileStringA(0, 0, 0, v9x_path);

    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0cu);
    v9x_outpb(V9X_VGA_CRTC_DATA, (BYTE)(V9X_START_MOVE_WORDS >> 8));
    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0du);
    v9x_outpb(V9X_VGA_CRTC_DATA, (BYTE)(V9X_START_MOVE_WORDS & 0xffu));
    v9x_key("Start", "ReadBack",
            ((DWORD)v9x_crtc_read(0x0cu) << 8) | (DWORD)v9x_crtc_read(0x0du));
    WritePrivateProfileStringA(0, 0, 0, v9x_path);
    Sleep(V9X_START_MOVE_HOLD_MS);

    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0cu);
    v9x_outpb(V9X_VGA_CRTC_DATA, high);
    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0du);
    v9x_outpb(V9X_VGA_CRTC_DATA, low);
    v9x_key("Start", "Restored",
            ((DWORD)v9x_crtc_read(0x0cu) << 8) | (DWORD)v9x_crtc_read(0x0du));
    v9x_outpb(V9X_VGA_CRTC_INDEX, saved_index);
    v9x_text("Start", "Result", "restored");
}

/*
 * /starthigh: the start address past CR0C/CR0D's 64 Ki dwords. /startmove
 * (boot 43) moved the picture 16 rows for 2000h, so the unit is a dword
 * and the head follows the VGA start. Riva/NV4's CR19 (behind lock CR1F)
 * read 0 and moved nothing on this card. The card's own VBIOS (VBE 4F07,
 * BL=02h, at C000:6ACE, writer at C000:5326) puts dword bits 15:0 in
 * CR0C/CR0D, 23:16 in CR35 and 29:24 in CR34 (masked 3Fh), with the int 10h
 * entry unlocking by CR3F = 57h and writing the old value back on exit.
 * This writes start = 10000h dwords (256 KiB, 128 rows at a 2048-byte
 * pitch) as CR0C/CR0D = 0 and CR35 = 01h, holds, and restores CR35,
 * CR0C/CR0D and CR3F as found. Also counts 3DAh bit 3 rising edges over
 * V9X_VBLANK_SAMPLE_MS, the retrace the flip state machine waits on.
 */
#define V9X_VGA_INPUT_STATUS_1 ((WORD)0x03dau)
#define V9X_VGA_VRETRACE       0x08u
#define V9X_NV50_CR_LOCK       0x3fu
#define V9X_NV50_CR_START_HI   0x34u
#define V9X_NV50_CR_START_MID  0x35u
#define V9X_NV50_UNLOCK        0x57u
#define V9X_VBLANK_SAMPLE_MS   500ul

static void v9x_start_high(void)
{
    DWORD edges = 0ul;
    DWORD started;
    BYTE saved_index;
    BYTE lock;
    BYTE high;
    BYTE low;
    BYTE mid;
    BYTE top;
    BYTE was = 0u;
    BYTE now;

    if ((v9x_inpb(V9X_VGA_MISC_READ) & 0x01u) == 0u) {
        v9x_text("Start", "Result", "mono-crtc-not-written");
        return;
    }
    started = GetTickCount();
    while (GetTickCount() - started < V9X_VBLANK_SAMPLE_MS) {
        now = (BYTE)(v9x_inpb(V9X_VGA_INPUT_STATUS_1) & V9X_VGA_VRETRACE);
        if (now != 0u && was == 0u) {
            ++edges;
        }
        was = now;
    }
    v9x_number("Start", "RetraceEdges500ms", edges);

    saved_index = v9x_inpb(V9X_VGA_CRTC_INDEX);
    lock = v9x_crtc_read(V9X_NV50_CR_LOCK);
    v9x_key("Start", "LockBefore", lock);
    v9x_outpb(V9X_VGA_CRTC_DATA, V9X_NV50_UNLOCK);
    v9x_key("Start", "LockUnlocked", v9x_crtc_read(V9X_NV50_CR_LOCK));
    high = v9x_crtc_read(0x0cu);
    low = v9x_crtc_read(0x0du);
    mid = v9x_crtc_read(V9X_NV50_CR_START_MID);
    top = v9x_crtc_read(V9X_NV50_CR_START_HI);
    v9x_key("Start", "Before", ((DWORD)top << 24) | ((DWORD)mid << 16) |
                               ((DWORD)high << 8) | (DWORD)low);
    v9x_text("Start", "Result", "holding");
    WritePrivateProfileStringA(0, 0, 0, v9x_path);

    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0cu);
    v9x_outpb(V9X_VGA_CRTC_DATA, 0u);
    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0du);
    v9x_outpb(V9X_VGA_CRTC_DATA, 0u);
    v9x_outpb(V9X_VGA_CRTC_INDEX, V9X_NV50_CR_START_MID);
    v9x_outpb(V9X_VGA_CRTC_DATA, 0x01u);
    v9x_key("Start", "MidWritten", v9x_crtc_read(V9X_NV50_CR_START_MID));
    WritePrivateProfileStringA(0, 0, 0, v9x_path);
    Sleep(V9X_START_MOVE_HOLD_MS);

    v9x_outpb(V9X_VGA_CRTC_INDEX, V9X_NV50_CR_START_MID);
    v9x_outpb(V9X_VGA_CRTC_DATA, mid);
    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0cu);
    v9x_outpb(V9X_VGA_CRTC_DATA, high);
    v9x_outpb(V9X_VGA_CRTC_INDEX, 0x0du);
    v9x_outpb(V9X_VGA_CRTC_DATA, low);
    v9x_key("Start", "MidRestored", v9x_crtc_read(V9X_NV50_CR_START_MID));
    v9x_outpb(V9X_VGA_CRTC_INDEX, V9X_NV50_CR_LOCK);
    v9x_outpb(V9X_VGA_CRTC_DATA, lock);
    v9x_key("Start", "LockAfter", v9x_crtc_read(V9X_NV50_CR_LOCK));
    v9x_outpb(V9X_VGA_CRTC_INDEX, saved_index);
    v9x_text("Start", "Result", "restored");
}

/* The PLL a control and coefficient pair name, P from the control. */
static DWORD v9x_pll_matches(DWORD ctrl, DWORD coef, DWORD n, DWORD m,
                             DWORD p)
{
    return (ctrl & V9X_NV50_PLL_ENABLE) != 0ul &&
           ((ctrl >> V9X_NV50_PLL_P_SHIFT) & V9X_NV50_PLL_P_MASK) == p &&
           (coef & V9X_NV50_PLL_COEF_MASK) == ((n << 8) | m);
}

static DWORD v9x_clocks_are(const struct v9x_g96_clocks *clocks)
{
    return v9x_pll_matches(v9x_value(V9X_NV50_NVPLL_CTRL),
                           v9x_value(V9X_NV50_NVPLL_COEF), clocks->core_n,
                           clocks->core_m, clocks->core_p) &&
           v9x_pll_matches(v9x_value(V9X_NV50_SPLL_CTRL),
                           v9x_value(V9X_NV50_SPLL_COEF), clocks->shader_n,
                           clocks->shader_m, clocks->shader_p);
}

/* /reclock and /reclock:post. Refuses unless the clocks are in a state
 * this tool has decoded and the VID line gives level 3's voltage; then
 * runs the script and reads the PLLs back. */
static void v9x_reclock(DWORD config, const struct v9x_g96_clocks *to)
{
    DWORD words;
    DWORD vid;
    DWORD done;
    char key[24];
    DWORD index;

    v9x_request.config = config;
    v9x_request_list(v9x_clock, sizeof(v9x_clock) / sizeof(v9x_clock[0]));
    if (!v9x_capture()) {
        v9x_finish("CLOCK-CAPTURE-FAILED", 2u);
    }
    v9x_publish_mmio();
    if (((v9x_result.boot0 >> V9X_NV50_CHIPSET_SHIFT) &
         V9X_NV50_CHIPSET_MASK) != V9X_NV50_CHIPSET_G96) {
        v9x_finish("NOT-G96", 2u);
    }

    /* Only from a state this tool has decoded: both PLLs on the PCIe
     * reference, core and shader on their PLLs, the PLLs at POST's or
     * level 3's values. */
    if ((v9x_value(V9X_NV50_CLK_REF_SELECT) & V9X_NV50_REF_MASK) !=
            V9X_NV50_REF_BOTH_HREF ||
        (v9x_value(V9X_NV50_CLK_SOURCE) & V9X_NV50_SRC_HOOK_MASK) !=
            V9X_NV50_SRC_HOOK ||
        (!v9x_clocks_are(&v9x_g96_post) &&
         !v9x_clocks_are(&v9x_g96_level3))) {
        v9x_finish("UNEXPECTED-CLOCK-STATE", 2u);
    }
    if (v9x_clocks_are(to)) {
        v9x_finish("ALREADY", 0u);
    }
    vid = ((v9x_value(V9X_NV50_GPIO_0_7) >> V9X_G96_VID_LINE_SHIFT) &
           V9X_G96_VID_SENSE) != 0ul ? 1ul : 0ul;
    v9x_number("Report", "Vid", vid);
    if (vid != V9X_G96_VID_LEVEL3) {
        v9x_finish("VOLTAGE-NOT-LEVEL3", 2u);
    }

    v9x_sq_build(to, v9x_value(V9X_NV50_CLK_SOURCE),
                 v9x_value(V9X_NV50_NVPLL_CTRL),
                 v9x_value(V9X_NV50_NVPLL_COEF),
                 v9x_value(V9X_NV50_SPLL_CTRL),
                 v9x_value(V9X_NV50_SPLL_COEF));
    words = (v9x_sq.size + 4ul) / 4ul;
    v9x_number("Report", "ScriptWords", words);
    v9x_request.count = words;
    for (index = 0ul; index < words; ++index) {
        v9x_request.offsets[index] =
            (DWORD)v9x_sq.code[index * 4ul] |
            ((DWORD)v9x_sq.code[index * 4ul + 1ul] << 8) |
            ((DWORD)v9x_sq.code[index * 4ul + 2ul] << 16) |
            ((DWORD)v9x_sq.code[index * 4ul + 3ul] << 24);
        v9x_offset_key(key, "Script", index, 2);
        v9x_key("Hwsq", key, v9x_request.offsets[index]);
    }
    if (!v9x_call(V9X_N5MM_HWSQ)) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_finish("HWSQ-FAILED", 3u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "SpinsLeft", v9x_result.reserved[0]);

    /* The readback replaces the values the checks above used. */
    v9x_value_count = 0ul;
    v9x_publish_mmio();
    if ((v9x_result.status & V9X_ST_HWSQ_TIMEOUT) != 0ul) {
        v9x_finish("HWSQ-TIMEOUT", 3u);
    }
    done = v9x_clocks_are(to);
    v9x_finish(done ? "COMPLETE" : "READBACK-DIFFERS", done ? 0u : 3u);
}

void __stdcall V9xNv50MmioEntry(void)
{
    DWORD config;
    DWORD enable;
    DWORD window;
    DWORD instance;
    int fb;
    int crtc;
    int fbwrite;
    int startmove;

    /* /fbwrite first: the switch scan matches prefixes, and "/fb" is one. */
    fbwrite = v9x_switch_given("/fbwrite");
    fb = !fbwrite && v9x_switch_given("/fb");
    crtc = v9x_switch_given("/crtc");
    startmove = v9x_switch_given("/startmove");
    /* /rom: the shadowed video BIOS at C0000h, 64 KiB, copied to
     * C:\V9XDIAG\V9XN5ROM.BIN for disassembly on the host (its VBE 4F07
     * handler says how the BIOS reaches start offsets past 256 KiB).
     * Win9x maps the system VM's first megabyte at linear 0; the read is
     * checked first and nothing is written but the file. */
    if (v9x_switch_given("/rom")) {
        const BYTE *rom = (const BYTE *)0x000c0000ul;
        HANDLE file;
        DWORD written = 0ul;

        v9x_path = V9X_N5VG_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "rom-shadow-read");
        if (IsBadReadPtr(rom, 0x10000u)) {
            v9x_finish("ROM-NOT-READABLE", 2u);
        }
        file = CreateFileA(V9X_DIAG_PATH("V9XN5ROM.BIN"), GENERIC_WRITE, 0,
                           0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) {
            v9x_finish("ROM-FILE-FAILED", 2u);
        }
        WriteFile(file, rom, 0x10000ul, &written, 0);
        CloseHandle(file);
        v9x_key("Report", "Signature",
                (DWORD)rom[0] | ((DWORD)rom[1] << 8) | ((DWORD)rom[2] << 16));
        v9x_number("Report", "Written", written);
        v9x_finish("COMPLETE", 0u);
    }
    /* /prom: the whole video BIOS through BAR0's PROM window (300000h),
     * 128 KiB, to C:\V9XDIAG\V9XN5PRM.BIN. The shadow at C0000h is the
     * image the BIOS trimmed after POST (6Fh x 512 bytes on the G96), and
     * its BIT 'P' perf table pointer lands in font data there. nouveau
     * reads the PROM this way when PBUS 1850h bit 0 (the ROM shadow) is
     * clear (subdev\bios\shadowrom.c, subdev\pci\base.c); the probe only
     * reads 1850h, and refuses if the bit is set. Reads only. */
    if (v9x_switch_given("/prom")) {
        HANDLE file;
        DWORD written = 0ul;
        DWORD base;
        DWORD index;

        v9x_path = V9X_N5VG_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "prom-read-only");
        config = v9x_find_gt218();
        if (config == 0ul) {
            v9x_finish("NO-GT218", 1u);
        }
        v9x_request.config = config;
        v9x_request.count = 0ul;
        v9x_request_add(V9X_NV50_ROM_ACCESS);
        if (!v9x_capture()) {
            v9x_finish("ROM-ACCESS-CAPTURE-FAILED", 2u);
        }
        v9x_key("Report", "RomAccess", v9x_result.first[0]);
        if ((v9x_result.first[0] & V9X_NV50_ROM_SHADOW) != 0ul) {
            v9x_finish("PROM-SHADOWED", 2u);
        }
        file = CreateFileA(V9X_DIAG_PATH("V9XN5PRM.BIN"), GENERIC_WRITE, 0,
                           0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) {
            v9x_finish("PROM-FILE-FAILED", 2u);
        }
        for (base = 0ul; base < V9X_NV50_PROM_BYTES;
             base += V9X_N5MM_OFFSET_MAX * 4ul) {
            v9x_request.count = 0ul;
            for (index = 0ul; index < V9X_N5MM_OFFSET_MAX; ++index) {
                v9x_request_add(V9X_NV50_PROM + base + index * 4ul);
            }
            if (!v9x_capture()) {
                CloseHandle(file);
                v9x_key("Report", "FailedAt", base);
                v9x_finish("PROM-CAPTURE-FAILED", 3u);
            }
            for (index = 0ul; index < V9X_N5MM_OFFSET_MAX; ++index) {
                if (v9x_result.first[index] != v9x_result.second[index]) {
                    v9x_key("Report", "Unstable", base + index * 4ul);
                }
            }
            WriteFile(file, v9x_result.first, V9X_N5MM_OFFSET_MAX * 4ul,
                      &written, 0);
        }
        CloseHandle(file);
        v9x_number("Report", "Written", V9X_NV50_PROM_BYTES);
        v9x_finish("COMPLETE", 0u);
    }
    /* /reclock: core and shader to the VBIOS's perf level 3; /reclock:post
     * back to the POST clocks. Report C:\V9XDIAG\V9XN5CK.INI. "/reclock"
     * is a prefix of "/reclock:post", so the longer one is asked first. */
    if (v9x_switch_given("/reclock")) {
        int post;

        post = v9x_switch_given("/reclock:post");
        v9x_path = V9X_N5CK_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "hwsq-clock-write");
        v9x_text("Report", "Target", post ? "post" : "level3");
        config = v9x_find_gt218();
        if (config == 0ul) {
            v9x_finish("NO-GT218", 1u);
        }
        v9x_reclock(config, post ? &v9x_g96_post : &v9x_g96_level3);
    }
    if (v9x_switch_given("/vga")) {
        DWORD offset;

        v9x_path = V9X_N5VG_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "mmio-read-only");
        config = v9x_find_gt218();
        if (config == 0ul) {
            v9x_finish("NO-GT218", 1u);
        }
        v9x_request.config = config;
        v9x_request.count = 0ul;
        v9x_request_add(0x6194e8ul);
        for (offset = 0x619e00ul; offset < 0x61a000ul; offset += 4ul) {
            v9x_request_add(offset);
        }
        if (!v9x_capture()) {
            v9x_key("Report", "Status", v9x_result.status);
            v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
            v9x_finish("VGA-CAPTURE-FAILED", 2u);
        }
        v9x_publish_mmio();
        v9x_finish("COMPLETE", 0u);
    }
    if (v9x_switch_given("/starthigh")) {
        v9x_path = V9X_N5SM_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "nv-start-high-write-restored");
        v9x_start_high();
        v9x_finish("COMPLETE", 0u);
    }
    if (startmove) {
        v9x_path = V9X_N5SM_PATH;
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        DeleteFileA(v9x_path);
        v9x_text("Report", "Tool", "V9XN5MM");
        v9x_text("Report", "Build", V9X_BUILD_ID);
        v9x_text("Report", "Access", "vga-start-write-restored");
        v9x_start_move();
        v9x_finish("COMPLETE", 0u);
    }
    if (fbwrite) {
        v9x_path = V9X_N5FW_PATH;
    } else if (fb) {
        v9x_path = V9X_N5FB_PATH;
    } else if (crtc) {
        v9x_path = V9X_N5CR_PATH;
    }
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(v9x_path);
    v9x_text("Report", "Tool", "V9XN5MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access",
             fbwrite ? "video-memory-write-restored" :
             crtc && !fb ? "vga-crtc-read" :
             fb ? "video-memory-read-only" : "mmio-read-only");

    config = v9x_find_gt218();
    if (config == 0ul) {
        v9x_finish("NO-GT218", 1u);
    }
    v9x_key("Report", "Config", config);

    if (fbwrite) {
        v9x_request.config = config;
        v9x_fb_write();
        v9x_finish("COMPLETE", 0u);
    }
    if (fb) {
        v9x_request.config = config;
        for (window = 0ul; window < V9X_N5_FB_WINDOWS; ++window) {
            v9x_fb_window(window);
        }
        v9x_finish("COMPLETE", 0u);
    }
    if (crtc) {
        v9x_crtc_probe();
        v9x_finish("COMPLETE", 0u);
    }
    v9x_publish_config(config);

    v9x_request.config = config;
    v9x_request_list(v9x_global, sizeof(v9x_global) / sizeof(v9x_global[0]));
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "Boot0", v9x_result.boot0);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        v9x_finish("GLOBAL-CAPTURE-FAILED", 2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "Boot0", v9x_result.boot0);
    v9x_publish_mmio();
    v9x_decode_global();

    v9x_request_heads();
    if (!v9x_capture()) {
        v9x_finish("HEAD-CAPTURE-FAILED", 3u);
    }
    v9x_publish_mmio();
    v9x_decode_heads();

    enable = v9x_value(V9X_NV50_PMC_ENABLE);
    v9x_optional(v9x_pfifo, sizeof(v9x_pfifo) / sizeof(v9x_pfifo[0]),
                 enable, V9X_NV50_ENABLE_PFIFO, "PfifoCapture",
                 "PFIFO-CAPTURE-FAILED");
    v9x_optional(v9x_pgraph, sizeof(v9x_pgraph) / sizeof(v9x_pgraph[0]),
                 enable, V9X_NV50_ENABLE_PGRAPH, "PgraphCapture",
                 "PGRAPH-CAPTURE-FAILED");
    /* The copy engine is the GT215's; a G96 has none (nouveau
     * nv96_chipset), and its PMC_ENABLE bits mean other engines there
     * (subdev\mc\g84.c), so 104000h is not read on it. */
    if (((v9x_result.boot0 >> V9X_NV50_CHIPSET_SHIFT) &
         V9X_NV50_CHIPSET_MASK) == V9X_NV50_CHIPSET_GT218) {
        v9x_optional(v9x_copy, sizeof(v9x_copy) / sizeof(v9x_copy[0]),
                     enable, V9X_NV50_ENABLE_CE, "CopyCapture",
                     "COPY-CAPTURE-FAILED");
    } else {
        v9x_text("Report", "CopyCapture", "skipped-no-copy-engine");
    }
    if (((v9x_result.boot0 >> V9X_NV50_CHIPSET_SHIFT) &
         V9X_NV50_CHIPSET_MASK) == V9X_NV50_CHIPSET_G96) {
        v9x_request_list(v9x_clock, sizeof(v9x_clock) / sizeof(v9x_clock[0]));
        if (!v9x_capture()) {
            v9x_finish("CLOCK-CAPTURE-FAILED", 3u);
        }
        v9x_publish_mmio();
        v9x_text("Report", "ClockCapture", "taken");
    } else {
        v9x_text("Report", "ClockCapture", "skipped-not-g96");
    }

    /* The window's first KiB. */
    v9x_request_pramin(0ul, V9X_PRAMIN_DENSE_BYTES, 4ul);
    if (!v9x_capture()) {
        v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
    }
    v9x_publish_pramin();

    /* The BAR VM instance's first KiB, when the window covers it. */
    window = v9x_value(V9X_NV50_PRAMIN_WINDOW) << V9X_NV50_WINDOW_SHIFT;
    instance = (v9x_value(V9X_NV50_BAR_VM) & V9X_NV50_VM_ADDR_MASK) <<
               V9X_NV50_VM_SHIFT;
    if (instance >= window && instance - window < V9X_NV50_PRAMIN_BYTES) {
        v9x_request_pramin(instance - window, V9X_PRAMIN_DENSE_BYTES, 4ul);
        if (!v9x_capture()) {
            v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
        }
        v9x_publish_pramin();
        v9x_text("Report", "InstanceCapture", "taken");
    } else {
        v9x_text("Report", "InstanceCapture", "outside-window");
    }

    /* One word every 4 KiB of the window. */
    v9x_request_pramin(0ul, V9X_NV50_PRAMIN_BYTES, V9X_PRAMIN_COARSE_STEP);
    if (!v9x_capture()) {
        v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
    }
    v9x_publish_pramin();

    v9x_finish("COMPLETE", 0u);
}
