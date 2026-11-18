; AMD Oland (Radeon R7 240, GCN 1) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-oland-gfx6.md, and kit 1 for an owner who is
; not on remote help. A copy of the Mullins probe (cik_mmio_probe.asm)
; with Southern Islands' windows. Maps the 256 KiB register BAR (BAR2, at
; configuration 18h, which Linux radeon and amdgpu map as rmmio on every
; SI part) and reads a caller's list of offsets, each twice.
;
; Writes nothing on the device or in memory. 0CF8h is saved and restored
; around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state. Not yet run on any card.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not an Oland in
; Linux's table (drm_pciids.h, amdgpu_drv.c: 6600-6608, 6610, 6611, 6613,
; 6617, 6620, 6621, 6623, 6631) with a 256 KiB-aligned memory BAR2 below
; 4 GiB. BAR2 may be 64-bit; its high half (1Ch) must then be zero.
;
; The identity check after mapping is Evergreen's: CONFIG_MEMSIZE (5428h,
; MiB, Linux si.c si_mc_init) must be non-zero and at most 4096. A window
; that decodes nothing reads FFFFFFFFh and fails it.
;
; Offsets are accepted only inside SIWindows below, and only for the first
; two display controllers (dce_6_0_d.h: the second at +C00h). Left out on
; purpose, because a read has a side effect or reads through an index:
;   0000h-001Bh  MM_INDEX / MM_DATA and MM_INDEX_HI (bif_3_0_d.h);
;   0200h-020Fh  SMC_IND_INDEX / _DATA (radeon sid.h);
;   0314h, 031Ch VGA_TEST_DEBUG_INDEX / _DATA;
;   5480h        HDP_MEM_COHERENCY_FLUSH_CNTL, a flush trigger;
;   6E84h-6E8Bh  CRTC_PIXEL_DATA_READBACK0/1 (evergreen_reg.h);
;   C150h-C17Fh  the CP microcode address/data ports (gfx_6_0_d.h);
;   C32Ch-C333h  RLC_UCODE_ADDR / _DATA;
;   D0D8h-D0E3h  DMA_PGFSM_CONFIG / _WRITE / _READ, and DMA1's at D8D8h.
; Offsets are Linux amdgpu's register headers (dword indices times four;
; build\external\carrizo\linux\), not checked against any AMD register
; guide.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device SIMM, 1, 0, SI_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

SI_MAGIC                equ 4d4d4953h ; "SIMM"
SI_DIOC_CAPTURE         equ 1
; DIOC 2: the one write path. A list of register writes and bounded polls
; (V9XSIMM /seq), checked whole before anything runs: writes only to the
; engine PLL and the MC arbiter timings SIWriteRegs names, polls only on
; SPLL_STATUS. Made for replaying the VBIOS's SetEngineClock sequence
; (docs\decisions\2026-11-18-oland-clocks-left-by-the-bios.md).
SI_DIOC_SEQUENCE        equ 2
SI_SEQ_MAX              equ 160
SI_SEQ_WRITE            equ 1
SI_SEQ_POLL_SET         equ 2
SI_SEQ_DELAY_US         equ 3
; About a second of status reads at a microsecond or so each.
SI_SEQ_POLL_LIMIT       equ 1000000
SI_SPLL_STATUS          equ 0614h
SI_MPLL_CNTL_MODE       equ 2bb0h
; A delay is reads of SRBM_STATUS: four a microsecond at least, since one
; uncached PCIe read takes longer than 250 ns. Longer than asked is the safe
; side for the DLL and PLL waits these are.
SI_SRBM_STATUS          equ 0e50h
SI_DELAY_READS_PER_US   equ 4
SI_DELAY_US_MAX         equ 1000
SI_VENDOR_ATI           equ 1002h
SI_CFG_BAR2             equ 18h
SI_CFG_BAR3             equ 1ch
SI_MMIO_BYTES           equ 00040000h
SI_BASE_ALIGN           equ 0003ffffh
SI_BAR_IO               equ 00000001h
SI_BAR_TYPE_MASK        equ 00000006h
SI_BAR_TYPE_64          equ 00000004h
SI_CONFIG_MEMSIZE       equ 5428h
SI_MEMSIZE_MAX_MIB      equ 4096
SI_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
SI_ST_ATI               equ 00000001h
SI_ST_BASE_INVALID      equ 00000002h
SI_ST_MMIO_MAPPED       equ 00000004h
SI_ST_IDENTITY_MATCH    equ 00000008h
SI_ST_OFFSETS_REFUSED   equ 00000010h
SI_ST_SNAPSHOT_TAKEN    equ 00000020h
SI_ST_BAR_64            equ 00000040h
SI_ST_NOT_OLAND         equ 00000080h
SI_ST_ADDRESS_INVALID   equ 00000100h
SI_ST_BAR_ABOVE_4G      equ 00000200h
SI_ST_SEQ_REFUSED       equ 00000400h
SI_ST_SEQ_TIMEOUT       equ 00000800h
SI_ST_SEQ_DONE          equ 00001000h

; Input from V9XSIMM.EXE.
SI_IN_CONFIG            equ 0
SI_IN_COUNT             equ 4
SI_IN_OFFSETS           equ 8
SI_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_simm_result in
; si_mmio_probe_win32.c.
SI_HEADER_DWORDS        equ 16
SI_RESULT_DWORDS        equ (SI_HEADER_DWORDS + SI_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Linux's Oland device ids.
SIDevices label word
    dw 6600h, 6601h, 6602h, 6603h, 6604h, 6605h, 6606h, 6607h, 6608h
    dw 6610h, 6611h, 6613h, 6617h, 6620h, 6621h, 6623h, 6631h
SI_DEVICE_COUNT         equ 17

; The registers DIOC 2 may write: CG_SPLL_FUNC_CNTL, _2, _3, 0610h,
; SPLL_CNTL_MODE, CG_SPLL_SPREAD_SPECTRUM (radeon sid.h), and the arbiter's
; MC_ARB_DRAM_TIMING, _TIMING2, MC_ARB_RFSH_RATE, MC_ARB_BURST_TIME
; (gmc_6_0_d.h), the ones SetEngineClock writes on this board.
SIWriteRegs label dword
    dd 0600h, 0604h, 0608h, 0610h, 0618h, 0620h
    dd 2774h, 2778h, 27b0h, 2808h
; And SetMemoryClock's (gmc_6_0_d.h): the blackout, BIF_FB_EN, the arbiter's
; refresh control, the sequencer's timings, commands and mode registers,
; the DLL and the memory PLL.
    dd 20ach, 5490h, 27ach
    dd 2888h, 2898h, 28a0h, 28a4h, 28a8h, 28ach, 28b0h, 28bch, 28c0h
    dd 28c4h, 292ch, 2944h, 295ch, 297ch, 2a04h, 2a08h, 2a0ch, 2a68h
    dd 2aach, 2b44h, 2b54h
    dd 2ba0h, 2ba4h, 2bb0h, 2bb4h, 2bb8h, 2bc0h, 2bc4h, 2bc8h, 2bd4h
SI_WRITE_REG_COUNT      equ 43

; Accepted windows: start, limit (exclusive). The display blocks repeat at
; the controller offsets 0 and C00h (dce_6_0_d.h: DCP1_GRPH_ENABLE 1D00h
; and CRTC1_CRTC_CONTROL 1E9Ch, dword indices).
SIWindows label dword
    dd 0300h, 0314h     ; VGA_RENDER_CONTROL .. VGA_MEMORY_BASE_ADDRESS
    dd 0318h, 031ch     ; VGA_DISPBUF1_SURFACE_ADDR
    dd 0320h, 0340h     ; VGA_DISPBUF2 .. VGA_HW_DEBUG
    dd 0600h, 0614h     ; CG_SPLL_FUNC_CNTL .. _4, 0610h (radeon sid.h, si_dpm.c)
    dd 1734h, 1738h     ; unnamed, read by the VBIOS's MC arbiter table
    dd 2774h, 277ch     ; MC_ARB_DRAM_TIMING, MC_ARB_DRAM_TIMING2
    dd 27ach, 27b4h     ; MC_ARB_RFSH_CNTL, MC_ARB_RFSH_RATE
    dd 2808h, 280ch     ; MC_ARB_BURST_TIME
    ; The memory sequencer registers the VBIOS's SetMemoryClock reads
    ; (gmc_6_0_d.h); its index/data pairs at 2A44h and 2B3Ch stay out.
    dd 20ach, 20b0h     ; MC_SHARED_BLACKOUT_CNTL
    dd 2888h, 288ch     ; MC_SEQ_MPLL_OVERRIDE
    dd 2898h, 289ch     ; MC_SEQ_DRAM
    dd 28a0h, 28c8h     ; MC_SEQ_RAS_TIMING .. MC_SEQ_CMD
    dd 292ch, 2930h     ; MC_IO_TXCNTL_APHY_D0
    dd 2944h, 2948h     ; MC_IO_TXCNTL_APHY_D1
    dd 295ch, 2960h     ; MC_SEQ_FIFO_CTL
    dd 297ch, 2980h     ; MC_SEQ_MISC8
    dd 2a04h, 2a10h     ; MC_SEQ_MISC1, MC_SEQ_RESERVE_M, MC_PMG_CMD_EMRS
    dd 2a24h, 2a28h     ; MC_SEQ_SUP_PGM_STAT
    dd 2a68h, 2a6ch     ; MC_SEQ_CG
    dd 2aach, 2ab0h     ; MC_PMG_CMD_MRS
    dd 2b44h, 2b48h     ; MC_PMG_CMD_MRS1
    dd 2b54h, 2b58h     ; MC_SEQ_WR_CTL_2
    dd 0614h, 061ch     ; SPLL_STATUS, SPLL_CNTL_MODE
    dd 0620h, 0628h     ; CG_SPLL_SPREAD_SPECTRUM, _2
    dd 065ch, 0668h     ; MPLL_BYPASSCLK_SEL, CG_CLKPIN_CNTL, _2
    dd 0780h, 0784h     ; GENERAL_PWRMGT
    dd 0788h, 078ch     ; SCLK_PWRMGT_CNTL
    dd 0798h, 079ch     ; TARGET_AND_CURRENT_PROFILE_INDEX
    dd 0714h, 0718h     ; CG_MULT_THERMAL_STATUS (radeon sid.h si_get_temp)
    dd 0e4ch, 0e54h     ; SRBM_STATUS2, SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 1400h, 1404h     ; VM_L2_CNTL
    dd 1410h, 1418h     ; VM_CONTEXT0_CNTL, VM_CONTEXT1_CNTL
    dd 153ch, 1540h     ; VM_CONTEXT0_PAGE_TABLE_BASE_ADDR
    dd 155ch, 1560h     ; VM_CONTEXT0_PAGE_TABLE_START_ADDR
    dd 157ch, 1580h     ; VM_CONTEXT0_PAGE_TABLE_END_ADDR
    dd 2004h, 2008h     ; MC_SHARED_CHMAP
    dd 2024h, 2040h     ; MC_VM_FB_LOCATION, AGP_TOP/BOT/BASE, SYSTEM_APERTURE_*
    dd 2064h, 206ch     ; MC_VM_MX_L1_TLB_CNTL, MC_VM_FB_OFFSET
    dd 2760h, 2764h     ; MC_ARB_RAMCFG
    dd 2a00h, 2a04h     ; MC_SEQ_MISC0
    dd 2ba0h, 2ba8h     ; MCLK_PWRMGT_CNTL, DLL_CNTL
    dd 2bb0h, 2bcch     ; MPLL_CNTL_MODE, MPLL_FUNC_CNTL .. MPLL_TIME
    dd 2bcch, 2bd8h     ; MPLL_SS1, MPLL_SS2, MPLL_CONTROL
    dd 2c00h, 2c10h     ; HDP_HOST_PATH_CNTL, HDP_NONSURFACE_BASE/INFO/SIZE
    dd 2f48h, 2f50h     ; HDP_ADDR_CONFIG, HDP_MISC_CNTL
    dd 5424h, 542ch     ; CONFIG_CNTL, CONFIG_MEMSIZE
    dd 5430h, 5434h     ; CONFIG_APER_SIZE
    dd 5490h, 5494h     ; BIF_FB_EN
    dd 8000h, 801ch     ; GRBM_CNTL .. GRBM_STATUS_SE1
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 802ch, 8030h     ; GRBM_GFX_INDEX
    dd 8500h, 8520h     ; SCRATCH_REG0-7
    dd 8674h, 8678h     ; CP_STALLED_STAT1
    dd 867ch, 8684h     ; CP_BUSY_STAT, CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 8700h, 8704h     ; CP_RB0_RPTR
    dd 8708h, 870ch     ; CP_RB_WPTR_POLL_CNTL
    dd 89bch, 89c4h     ; CC_GC_SHADER_ARRAY_CONFIG, GC_USER_SHADER_ARRAY_CONFIG
    dd 98f4h, 98fch     ; CC_RB_BACKEND_DISABLE, GB_ADDR_CONFIG
    dd 9910h, 9990h     ; GB_TILE_MODE0-31
    dd 0c100h, 0c118h   ; CP_RB0_BASE .. CP_RB0_WPTR
    dd 0c144h, 0c148h   ; CP_RB_VMID
    dd 0c1a8h, 0c1ach   ; CP_INT_CNTL_RING0
    dd 0c300h, 0c304h   ; RLC_CNTL
    dd 0c30ch, 0c310h   ; RLC_LB_CNTL
    dd 0c344h, 0c350h   ; RLC_MC_CNTL, RLC_UCODE_CNTL, RLC_STAT
    dd 0c35ch, 0c360h   ; RLC_PG_CNTL
    dd 0c404h, 0c408h   ; RLC_CGCG_CGLS_CTRL
    dd 0c464h, 0c468h   ; RLC_SERDES_MASTER_BUSY_0
    dd 0d000h, 0d010h   ; DMA_GFX_RB_CNTL .. DMA_GFX_RB_WPTR
    dd 0d01ch, 0d030h   ; DMA_GFX_RB_RPTR_ADDR_HI .. DMA_CNTL
    dd 0d034h, 0d038h   ; DMA_STATUS_REG
    dd 0d044h, 0d04ch   ; DMA_SEM_INCOMPLETE/WAIT_FAIL_TIMER_CNTL
    dd 0d0b8h, 0d0c4h   ; DMA_TILING_CONFIG, DMA_POWER_CNTL, DMA_CLK_CTRL
    dd 0d0d4h, 0d0d8h   ; DMA_PG
    dd 0d800h, 0d810h   ; DMA1 (+800h)
    dd 0d81ch, 0d830h
    dd 0d834h, 0d838h
    dd 0d8b8h, 0d8c4h
    ; Controller 0
    dd 6800h, 6850h     ; GRPH_ENABLE .. GRPH_SURFACE_ADDRESS_INUSE
    dd 6998h, 69bch     ; CUR_CONTROL .. CUR_UPDATE
    dd 6d70h, 6d78h     ; VIEWPORT_START, VIEWPORT_SIZE
    dd 6e00h, 6e04h     ; CRTC_H_TOTAL
    dd 6e1ch, 6e20h     ; CRTC_V_TOTAL
    dd 6e34h, 6e38h     ; CRTC_V_BLANK_START_END
    dd 6e70h, 6e78h     ; CRTC_CONTROL, CRTC_BLANK_CONTROL
    dd 6e8ch, 6e9ch     ; CRTC_STATUS .. CRTC_STATUS_FRAME_COUNT
    dd 6ea0h, 6ea4h     ; CRTC_STATUS_HV_COUNT
    dd 6ed4h, 6ed8h     ; CRTC_UPDATE_LOCK
    ; Controller 1 (+C00h)
    dd 7400h, 7450h
    dd 7598h, 75bch
    dd 7970h, 7978h
    dd 7a00h, 7a04h
    dd 7a1ch, 7a20h
    dd 7a34h, 7a38h
    dd 7a70h, 7a78h
    dd 7a8ch, 7a9ch
    dd 7aa0h, 7aa4h
    dd 7ad4h, 7ad8h
SI_WINDOW_COUNT         equ 100

SIResult label dword
    dd SI_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR3 (BAR2's high half) when 64-bit
    dd 0                        ; 10 reserved
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
SIOffsets    dd SI_OFFSET_MAX dup (0)
SIFirst      dd SI_OFFSET_MAX dup (0)
SISecond     dd SI_OFFSET_MAX dup (0)
SIConfig     dd 0
SIMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc SI_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     SIResult[24], eax
    xchg    eax, ebx
    out     dx, eax
    mov     dx, 0cfch
    in      eax, dx
    xchg    eax, ebx
    mov     dx, 0cf8h
    out     dx, eax
    mov     eax, ebx
    popfd
    pop     edx
    pop     ebx
    ret
EndProc SI_Pci_Read

; AX = device id. Returns CF clear if it is one of Linux's Oland ids.
BeginProc SI_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 SIDevices
    mov     ecx, SI_DEVICE_COUNT
SI_Device_Next:
    cmp     ax, [esi]
    je      short SI_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short SI_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
SI_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SI_Device_Known

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc SI_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short SI_Offset_Refused
    mov     esi, OFFSET32 SIWindows
    mov     ecx, SI_WINDOW_COUNT
SI_Offset_Next:
    cmp     edx, [esi]
    jb      short SI_Offset_Skip
    cmp     edx, [esi+4]
    jb      short SI_Offset_Accepted
SI_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short SI_Offset_Next
SI_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
SI_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SI_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc SI_Accept_Offsets
    pushad
    mov     SIResult[16], 0
    sub     ecx, SI_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+SI_IN_COUNT]
    cmp     eax, SI_OFFSET_MAX
    ja      short SI_Accept_Refused
    cmp     eax, ecx
    ja      short SI_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+SI_IN_OFFSETS]
    mov     edi, OFFSET32 SIOffsets
    jecxz   short SI_Accept_Done
SI_Accept_Next:
    mov     edx, [esi]
    call    SI_Offset_Allowed
    jc      short SI_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short SI_Accept_Next
SI_Accept_Done:
    mov     eax, [ebp+SI_IN_COUNT]
    mov     SIResult[16], eax
    popad
    ret
SI_Accept_Refused_At:
    mov     SIResult[28], edx
SI_Accept_Refused:
    or      SIResult[4], SI_ST_OFFSETS_REFUSED
    popad
    ret
EndProc SI_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc SI_Read_Snapshot
    pushad
    mov     ecx, SIResult[16]
    mov     ebx, OFFSET32 SIOffsets
    mov     esi, SIMmio
    jecxz   short SI_Read_Snapshot_Done
SI_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short SI_Read_Snapshot_Next
SI_Read_Snapshot_Done:
    popad
    ret
EndProc SI_Read_Snapshot

BeginProc SI_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, SIConfig
    mov     SIResult[32], eax
    test    eax, 80000000h
    jz      SI_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     SI_Capture_Bad_Address

    call    SI_Pci_Read
    mov     SIResult[8], eax
    cmp     ax, SI_VENDOR_ATI
    jne     SI_Capture_Bad_Base
    or      SIResult[4], SI_ST_ATI
    shr     eax, 16
    call    SI_Device_Known
    jnc     short SI_Capture_Check_Base
    or      SIResult[4], SI_ST_NOT_OLAND
    jmp     SI_Capture_Done

SI_Capture_Check_Base:
    mov     eax, SIConfig
    or      eax, SI_CFG_BAR2
    call    SI_Pci_Read
    mov     SIResult[12], eax
    test    eax, SI_BAR_IO
    jnz     SI_Capture_Bad_Base
    mov     edx, eax
    and     edx, SI_BAR_TYPE_MASK
    jz      short SI_Capture_Low_Base
    cmp     edx, SI_BAR_TYPE_64
    jne     SI_Capture_Bad_Base
    or      SIResult[4], SI_ST_BAR_64
    ; A 64-bit BAR2: its high half must be zero.
    push    eax
    mov     eax, SIConfig
    or      eax, SI_CFG_BAR3
    call    SI_Pci_Read
    mov     SIResult[36], eax
    test    eax, eax
    pop     eax
    jz      short SI_Capture_Low_Base
    or      SIResult[4], SI_ST_BAR_ABOVE_4G
    jmp     SI_Capture_Bad_Base

SI_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      SI_Capture_Bad_Base
    test    eax, SI_BASE_ALIGN
    jnz     SI_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,SI_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      SI_Capture_Done
    mov     SIMmio, eax
    or      SIResult[4], SI_ST_MMIO_MAPPED

    mov     edx, [eax+SI_CONFIG_MEMSIZE]
    mov     SIResult[20], edx
    test    edx, edx
    jz      short SI_Capture_Done
    cmp     edx, SI_MEMSIZE_MAX_MIB
    ja      short SI_Capture_Done
    or      SIResult[4], SI_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 SIFirst
    call    SI_Read_Snapshot
    mov     edi, OFFSET32 SISecond
    call    SI_Read_Snapshot
    or      SIResult[4], SI_ST_SNAPSHOT_TAKEN
    jmp     short SI_Capture_Done

SI_Capture_Bad_Address:
    or      SIResult[4], SI_ST_ADDRESS_INVALID
    jmp     short SI_Capture_Done
SI_Capture_Bad_Base:
    or      SIResult[4], SI_ST_BASE_INVALID
SI_Capture_Done:
    popad
    ret
EndProc SI_Capture

; EDX = offset. Returns CF clear if DIOC 2 may write it.
BeginProc SI_Write_Allowed
    push    ecx
    push    esi
    mov     esi, OFFSET32 SIWriteRegs
    mov     ecx, SI_WRITE_REG_COUNT
SI_Write_Next:
    cmp     edx, [esi]
    je      short SI_Write_Yes
    add     esi, 4
    dec     ecx
    jnz     short SI_Write_Next
    pop     esi
    pop     ecx
    stc
    ret
SI_Write_Yes:
    pop     esi
    pop     ecx
    clc
    ret
EndProc SI_Write_Allowed

; EBP = DIOC input (config, count, then count ops of op, offset, value),
; ECX = its bytes. Checks the whole list, then, with the card mapped and
; identified by SI_Capture, runs it. Per op: SIOffsets the offset, SIFirst
; the register read after it, SISecond the status reads a poll took.
; SIResult[16] the ops run. Stops at the first poll that runs out.
BeginProc SI_Sequence
    pushad
    sub     ecx, SI_IN_OFFSETS
    mov     eax, [ebp+SI_IN_COUNT]
    cmp     eax, SI_SEQ_MAX
    ja      SI_Seq_Refused
    lea     edx, [eax+eax*2]
    shl     edx, 2
    cmp     edx, ecx
    ja      SI_Seq_Refused

    ; Every op checked before any runs.
    mov     ecx, eax
    lea     esi, [ebp+SI_IN_OFFSETS]
    jecxz   short SI_Seq_Checked
SI_Seq_Check:
    mov     eax, [esi]
    mov     edx, [esi+4]
    cmp     eax, SI_SEQ_WRITE
    jne     short SI_Seq_Check_Poll
    call    SI_Write_Allowed
    jc      SI_Seq_Refused_At
    jmp     short SI_Seq_Check_Next
SI_Seq_Check_Poll:
    cmp     eax, SI_SEQ_POLL_SET
    jne     short SI_Seq_Check_Delay
    cmp     edx, SI_SPLL_STATUS
    je      short SI_Seq_Check_Next
    cmp     edx, SI_MPLL_CNTL_MODE
    jne     SI_Seq_Refused_At
    jmp     short SI_Seq_Check_Next
SI_Seq_Check_Delay:
    cmp     eax, SI_SEQ_DELAY_US
    jne     SI_Seq_Refused_At
    cmp     dword ptr [esi+8], SI_DELAY_US_MAX
    ja      SI_Seq_Refused_At
SI_Seq_Check_Next:
    add     esi, 12
    dec     ecx
    jnz     short SI_Seq_Check
SI_Seq_Checked:

    ; The card mapped and identified, with no snapshot offsets.
    mov     SIResult[16], 0
    call    SI_Capture
    test    SIResult[4], SI_ST_IDENTITY_MATCH
    jz      SI_Seq_Out
    mov     edi, SIMmio
    test    edi, edi
    jz      SI_Seq_Out

    ; Interrupts off for the whole list: SetMemoryClock takes the CPU's
    ; access to VRAM away (BIF_FB_EN) until its end, so nothing else may
    ; run in between. Every poll is bounded, so this ends.
    pushfd
    cli
    mov     ecx, [ebp+SI_IN_COUNT]
    lea     esi, [ebp+SI_IN_OFFSETS]
    xor     ebx, ebx
    test    ecx, ecx
    jz      SI_Seq_Done
SI_Seq_Run:
    mov     edx, [esi+4]
    mov     SIOffsets[ebx*4], edx
    mov     SISecond[ebx*4], 0
    cmp     dword ptr [esi], SI_SEQ_DELAY_US
    jne     short SI_Seq_Not_Delay
    push    ecx
    mov     ecx, [esi+8]
    imul    ecx, SI_DELAY_READS_PER_US
    jecxz   short SI_Seq_Delay_Done
SI_Seq_Delay_Read:
    mov     eax, [edi+SI_SRBM_STATUS]
    dec     ecx
    jnz     short SI_Seq_Delay_Read
SI_Seq_Delay_Done:
    pop     ecx
    mov     eax, [esi+8]
    mov     SIFirst[ebx*4], eax
    jmp     SI_Seq_Next
SI_Seq_Not_Delay:
    cmp     dword ptr [esi], SI_SEQ_WRITE
    jne     short SI_Seq_Poll
    mov     eax, [esi+8]
    mov     [edi+edx], eax
    mov     eax, [edi+edx]              ; read back: posts the write
    mov     SIFirst[ebx*4], eax
    jmp     short SI_Seq_Next
SI_Seq_Poll:
    push    ecx
    mov     ecx, SI_SEQ_POLL_LIMIT
SI_Seq_Poll_Read:
    mov     eax, [edi+edx]
    test    eax, [esi+8]
    jnz     short SI_Seq_Poll_Done
    dec     ecx
    jnz     short SI_Seq_Poll_Read
    pop     ecx
    mov     SIFirst[ebx*4], eax
    mov     SISecond[ebx*4], SI_SEQ_POLL_LIMIT
    inc     ebx
    mov     SIResult[16], ebx
    or      SIResult[4], SI_ST_SEQ_TIMEOUT
    popfd
    jmp     short SI_Seq_Out
SI_Seq_Poll_Done:
    mov     SIFirst[ebx*4], eax
    mov     eax, SI_SEQ_POLL_LIMIT
    sub     eax, ecx
    mov     SISecond[ebx*4], eax
    pop     ecx
SI_Seq_Next:
    add     esi, 12
    inc     ebx
    dec     ecx
    jnz     SI_Seq_Run
SI_Seq_Done:
    popfd
    mov     SIResult[16], ebx
    or      SIResult[4], SI_ST_SEQ_DONE
    jmp     short SI_Seq_Out

SI_Seq_Refused_At:
    mov     SIResult[28], edx
SI_Seq_Refused:
    or      SIResult[4], SI_ST_SEQ_REFUSED
SI_Seq_Out:
    popad
    ret
EndProc SI_Sequence

BeginProc SI_Reset_Result
    pushad
    mov     edi, OFFSET32 SIResult + 4
    mov     ecx, SI_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     SIMmio, eax
    popad
    ret
EndProc SI_Reset_Result

BeginProc SI_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      SI_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      SI_Dioc_Ok
    cmp     ecx, SI_DIOC_CAPTURE
    je      short SI_Dioc_Accept
    cmp     ecx, SI_DIOC_SEQUENCE
    jne     SI_Dioc_Fail

SI_Dioc_Accept:
    pushad
    mov     edx, ecx                    ; which call
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short SI_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, SI_IN_MIN_BYTES
    jb      short SI_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short SI_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], SI_RESULT_DWORDS * 4
    jb      short SI_Dioc_Copy_Fail

    call    SI_Reset_Result
    mov     eax, [ebp+SI_IN_CONFIG]
    mov     SIConfig, eax
    cmp     edx, SI_DIOC_SEQUENCE
    jne     short SI_Dioc_Snapshot
    call    SI_Sequence
    jmp     short SI_Dioc_Copy_Out
SI_Dioc_Snapshot:
    call    SI_Accept_Offsets
    test    SIResult[4], SI_ST_OFFSETS_REFUSED
    jnz     short SI_Dioc_Copy_Out
    call    SI_Capture

SI_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 SIResult
    mov     ecx, SI_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short SI_Dioc_Copy_Done
    mov     dword ptr [eax], SI_RESULT_DWORDS * 4
SI_Dioc_Copy_Done:
    popad
SI_Dioc_Ok:
    xor     eax, eax
    ret

SI_Dioc_Copy_Fail:
    popad
SI_Dioc_Fail:
    mov     eax, 1
    ret
EndProc SI_W32_DeviceIoControl

BeginProc SI_Dynamic_Init
    clc
    ret
EndProc SI_Dynamic_Init

BeginProc SI_Dynamic_Exit
    clc
    ret
EndProc SI_Dynamic_Exit

Begin_Control_Dispatch SI
    Control_Dispatch Sys_Dynamic_Device_Init, SI_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, SI_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, SI_W32_DeviceIoControl
End_Control_Dispatch SI

VxD_LOCKED_CODE_ENDS

end
