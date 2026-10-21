; Ivy Bridge (Gen7) read-only MMIO and GTT probe VxD.
;
; Step 3 of docs\plans\intel-ivybridge-gen7.md. Loaded dynamically by
; V9XIVMM.EXE, which passes the GTTMMADR base it read from config space and
; a list of register offsets. GTTMMADR is 4 MB: registers in the low 2 MB,
; the GTT at +2 MB (PRM Vol 1 Pt 2, "GTTMMADR"; the same split the Haswell
; path measured on the i5-4570). This maps both halves, reads every listed
; offset twice, copies the first IV_GTT_COPY GTT entries - 64 MB of
; aperture, the stolen size on the machine this was written for - and
; counts the valid entries in the whole table.
;
; DIOC 1, the display capture, writes nothing to the device or to memory.
; 0CF8h is saved and restored around the two configuration reads (the
; IGD's ID, and GGC from the host bridge to size the GTT). Mapping a
; physical range with _MapPhysToLinear changes no device state.
;
; DIOC 1 takes no forcewake. i915 (intel_uncore.c, NEEDS_FORCE_WAKE) treats
; every Gen6/7 register below 40000h or at 116000h and above as needing
; the GT awake; the window between is display and PCH, and is what DIOC 1
; reads. The one exception is the three forcewake acknowledge registers,
; which i915 reads raw before it holds forcewake, as it must.
;
; DIOC 2, the GT capture, is the only path that writes, and it writes only
; the two forcewake request registers, in i915's Ivy Bridge order
; (intel_uncore.c, the IS_IVYBRIDGE branch of the forcewake init,
; fw_domain_reset, fw_domains_get_with_thread_status):
;   FORCEWAKE (A18Ch) = 0, then a posting read of ECOBUS;
;   FORCEWAKE_MT (A188h) = FFFF0000h, every thread bit cleared
;     (WaRsClearFWBitsAtReset) - the BIOS leaves bit 0 acknowledged;
;   wait for FORCEWAKE_MT_ACK (130040h) bit 0 clear, else refuse;
;   FORCEWAKE_MT = 00010001h, wait for the acknowledge;
;   wait for GT thread status (13805Ch) bits 2:0 to read 0
;     (WaRsForcewakeWaitTC0);
;   read IV_GT_OFFSETS twice;
;   FORCEWAKE_MT = 00010000h, wait for the acknowledge to clear;
;   if bit 0 was acknowledged on entry, FORCEWAKE_MT = 00010001h again,
;     so the GT is left as it was found.
; Every wait is a bounded poll (IV_POLL_LIMIT reads, about i915's 50 ms
; budget) with interrupts off for the whole sequence. No other register is
; written, and no GT register outside IV_GT_OFFSETS is read.
;
; Offsets are accepted only inside IV_WINDOWS below, each window a named
; register or block from the IVB PRM Vol 3 Pt 3/4. Left out on purpose:
; the pipes' scanline and frame counters (70000h, 70040h-7004Ch), which
; hard-locked the 945 when read on a disabled pipe
; (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md);
; and the panel fitters' coefficient index and data ports (680A0h-680B4h),
; whose data port may step its index on a read. Which pipe's timing, link
; and plane registers to read is the publisher's decision, made from a
; first capture of PIPECONF and the transcoders' TRANS_CONF.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device IVMM, 1, 0, IV_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

IV_MAGIC                equ 4d4d5649h ; "IVMM"
IV_DIOC_CAPTURE         equ 1
IV_HOST_ADDRESS         equ 80000000h ; bus 0, device 0, function 0
IV_IGD_ADDRESS          equ 80001000h ; bus 0, device 2, function 0
IV_GGC_ADDRESS          equ 80000050h ; host bridge GGC, 50h
IV_VENDOR_INTEL         equ 8086h
IV_MMIO_BYTES           equ 00200000h
IV_GTT_OFFSET           equ 00200000h
IV_GTT_COPY             equ 16384
IV_OFFSET_MAX           equ 256
IV_BASE_ALIGN           equ 003fffffh  ; 4 MB, the BAR's own alignment
; GGC GGMS, bits 9:8: 0 none, 1 = 1 MB, 2 = 2 MB, 3 reserved (datasheet
; 326765, GGC).
IV_GGMS_SHIFT           equ 8
IV_GGMS_MASK            equ 3

; DIOC 2. Forcewake registers and values are i915's (intel_gt_regs.h;
; the PRM names none of them). The request registers are masked: the high
; word selects the bits the low word writes.
IV_DIOC_GT              equ 2
IV_GT_MAGIC             equ 54475649h ; "IVGT"
IV_FW_LEGACY            equ 0a18ch    ; FORCEWAKE
IV_FW_MT                equ 0a188h    ; FORCEWAKE_MT
IV_FW_MT_ACK            equ 130040h   ; FORCEWAKE_MT_ACK
IV_FW_LEGACY_ACK        equ 130090h   ; FORCEWAKE_ACK
IV_ECOBUS               equ 0a180h
IV_THREAD_STATUS        equ 13805ch   ; GEN6_GT_THREAD_STATUS_REG
IV_THREAD_MASK          equ 7
IV_FW_KERNEL            equ 1         ; FORCEWAKE_KERNEL, bit 0
IV_FW_CLEAR_ALL         equ 0ffff0000h
IV_FW_SET_KERNEL        equ 00010001h
IV_FW_CLEAR_KERNEL      equ 00010000h
IV_POLL_LIMIT           equ 100000
IV_GT_COUNT             equ 30

; DIOC 2 status bits.
IV_GT_ST_IGD_OK         equ 00000001h
IV_GT_ST_BAD_BASE       equ 00000002h
IV_GT_ST_NOT_IVB        equ 00000004h
IV_GT_ST_MAPPED         equ 00000008h
IV_GT_ST_ACK_STUCK      equ 00000010h ; acknowledge never cleared: refused
IV_GT_ST_SET_TIMEOUT    equ 00000020h
IV_GT_ST_THREAD_TIMEOUT equ 00000040h
IV_GT_ST_READ_DONE      equ 00000080h
IV_GT_ST_PUT_TIMEOUT    equ 00000100h
IV_GT_ST_RESTORED       equ 00000200h
IV_GT_ST_RESTORE_TIMEOUT equ 00000400h

; DIOC 2 output, in dwords. Must match struct v9x_ivgt_result.
IV_GT_HEADER_DWORDS     equ 24
IV_GT_RESULT_DWORDS     equ (IV_GT_HEADER_DWORDS + IV_GT_COUNT * 3)

; Status bits returned to the publisher.
IV_ST_IGD_INTEL         equ 00000001h
IV_ST_BASE_INVALID      equ 00000002h
IV_ST_MMIO_MAPPED       equ 00000004h
IV_ST_GTT_MAPPED        equ 00000008h
IV_ST_OFFSETS_REFUSED   equ 00000010h
IV_ST_SNAPSHOT_TAKEN    equ 00000020h
IV_ST_GTT_READ          equ 00000040h
IV_ST_NOT_IVB           equ 00000080h
IV_ST_GGMS_UNUSABLE     equ 00000100h

; Input from V9XIVMM.EXE.
IV_IN_BASE              equ 0
IV_IN_COUNT             equ 4
IV_IN_OFFSETS           equ 8
IV_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_ivmm_result in
; ivb_mmio_probe_win32.c.
IV_HEADER_DWORDS        equ 16
IV_RESULT_DWORDS        equ (IV_HEADER_DWORDS + IV_OFFSET_MAX * 3 + \
                             IV_GTT_COPY)

VxD_LOCKED_DATA_SEG
; Ivy Bridge IGD device ids (Linux 6.6 i915_pciids.h INTEL_IVB_M_IDS and
; INTEL_IVB_D_IDS): mobile GT1 0156, GT2 0166; desktop GT1 0152, GT2 0162;
; server GT1 015A, GT2 016A.
IVDevices label word
    dw 0152h, 0156h, 015ah, 0162h, 0166h, 016ah
IV_DEVICE_COUNT         equ 6

; Accepted windows: start, limit (exclusive). Offsets within the 2 MB of
; registers. Names are the IVB PRM's, i915's in brackets where they differ.
IVWindows label dword
    dd 041000h, 041004h   ; VGA_CONTROL [CPU_VGACNTRL]
    dd 045000h, 045014h   ; ARB_CTL, ARB_CTL2, MSG_CTL
    dd 045100h, 045130h   ; WM0 pipe A/B, WM_LP1-3, WM_LP*_SPR
    dd 045200h, 045204h   ; WM0 pipe C
    dd 060000h, 060050h   ; pipe A timings, SRCSZ, VSYNCSHIFT, data/link M/N
    dd 060100h, 060104h   ; FDI_TX_CTL_A
    dd 061000h, 061050h   ; pipe B, the same
    dd 061100h, 061104h   ; FDI_TX_CTL_B
    dd 062000h, 062050h   ; pipe C, the same
    dd 062100h, 062104h   ; FDI_TX_CTL_C
    dd 064000h, 064004h   ; DP_CTL_A (CPU eDP)
    dd 068060h, 068084h   ; panel fitter 0: power gate, position, size, CTL
    dd 068860h, 068884h   ; panel fitter 1, the same
    dd 070008h, 07000ch   ; PIPE_CONF_A
    dd 070180h, 0701b0h   ; primary plane A
    dd 071008h, 07100ch   ; PIPE_CONF_B
    dd 071180h, 0711b0h   ; primary plane B
    dd 072008h, 07200ch   ; PIPE_CONF_C
    dd 072180h, 0721b0h   ; primary plane C
    dd 0c6014h, 0c601ch   ; PCH DPLL A, B
    dd 0c6040h, 0c6050h   ; PCH FP0/FP1 A, B
    dd 0c6200h, 0c6208h   ; DREF_CTL, RAWCLK_FREQ
    dd 0c7000h, 0c7004h   ; DPLL_SEL
    dd 0e0000h, 0e0020h   ; transcoder A timings
    dd 0e0300h, 0e0304h   ; TRANS_DP_CTL_A
    dd 0e1000h, 0e1020h   ; transcoder B timings
    dd 0e1100h, 0e1104h   ; DAC_CTL [PCH_ADPA]
    dd 0e1140h, 0e1144h   ; HDMI_CTL_B
    dd 0e1150h, 0e1154h   ; HDMI_CTL_C
    dd 0e1160h, 0e1164h   ; HDMI_CTL_D
    dd 0e1180h, 0e1184h   ; LVDS_CTL
    dd 0e1300h, 0e1304h   ; TRANS_DP_CTL_B
    dd 0e2000h, 0e2020h   ; transcoder C timings
    dd 0e2300h, 0e2304h   ; TRANS_DP_CTL_C
    dd 0e4100h, 0e4104h   ; DP_CTL_B
    dd 0e4200h, 0e4204h   ; DP_CTL_C
    dd 0e4300h, 0e4304h   ; DP_CTL_D
    dd 0f0008h, 0f0010h   ; TRANS_CONF_A, FDI_RX_CTL_A
    dd 0f1008h, 0f1010h   ; TRANS_CONF_B, FDI_RX_CTL_B
    dd 0f2008h, 0f2010h   ; TRANS_CONF_C, FDI_RX_CTL_C
    dd 130040h, 130048h   ; FORCEWAKE_MT_ACK, FORCEWAKE_ACK_HSW (i915)
    dd 130090h, 130094h   ; FORCEWAKE_ACK (i915)
IV_WINDOW_COUNT         equ 42

IVResult label dword
    dd IV_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 valid GTT entries, whole table
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 GGC dword as read (50h-53h)
    dd 0                        ; 09 GTT bytes mapped
    dd 0                        ; 10 GTT entries in the table
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
IVOffsets    dd IV_OFFSET_MAX dup (0)
IVFirst      dd IV_OFFSET_MAX dup (0)
IVSecond     dd IV_OFFSET_MAX dup (0)
IVGtt        dd IV_GTT_COPY dup (0)
IVBase       dd 0
IVMmio       dd 0
IVGttLinear  dd 0

; The GT capture's data sits after the display result, never inside it:
; DIOC 1 copies IVResult through IVGtt as one block.
; DIOC 2's reads, all under forcewake. Ring offsets are the IVB PRM's
; (Vol 1 Pt 3 and Pt 4: TAIL 30h, HEAD 34h, START 38h, CTL 3Ch, ACTHD 74h,
; on the RCS base 2000h and the BCS base 22000h); MI_MODE, INSTPM and
; GFX_MODE are the PRM's too. The power, ECOBUS, L3 and GT-status offsets
; are i915's (intel_gt_regs.h), which is the only source naming them.
IVGtOffsets label dword
    dd 002030h, 002034h, 002038h, 00203ch ; RCS TAIL, HEAD, START, CTL
    dd 002074h                            ; RCS ACTHD
    dd 00209ch, 0020c0h, 00229ch          ; MI_MODE, INSTPM, GFX_MODE
    dd 022030h, 022034h, 022038h, 02203ch ; BCS TAIL, HEAD, START, CTL
    dd 022074h                            ; BCS ACTHD
    dd 004090h                            ; GAM_ECOCHK
    dd 007010h                            ; GEN7_COMMON_SLICE_CHICKEN1
    dd 00a01ch, 00a024h                   ; GEN6_RPSTAT1, GEN6_RP_CONTROL
    dd 00a090h, 00a094h                   ; GEN6_RC_CONTROL, GEN6_RC_STATE
    dd 00a180h                            ; ECOBUS
    dd 00b01ch, 00b020h, 00b024h          ; GEN7_L3CNTLREG1-3
    dd 00b030h, 00b034h                   ; L3 chicken mode, GEN7_L3SQCREG4
    dd 120000h, 120008h                   ; GTFIFODBG, GTFIFOCTL
    dd 13805ch, 138060h                   ; GT thread status, GT core status
    dd 138108h                            ; GEN6_GT_GFX_RC6 residency

IVGtResult label dword
    dd IV_GT_MAGIC              ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 IGD vendor/device as read
    dd 0                        ; 03 GTTMMADR base used
    dd 0                        ; 04 FORCEWAKE_MT_ACK on entry
    dd 0                        ; 05 FORCEWAKE_ACK on entry
    dd 0                        ; 06 GT thread status on entry
    dd 0                        ; 07 ECOBUS posting read, before any wake
    dd 0                        ; 08 polls: MT ack clear after the reset
    dd 0                        ; 09 polls: MT ack set
    dd 0                        ; 10 polls: thread status C0
    dd 0                        ; 11 polls: MT ack clear after the put
    dd 0                        ; 12 polls: MT ack set after the restore
    dd 0                        ; 13 FORCEWAKE_MT_ACK on exit
    dd 0                        ; 14 FORCEWAKE_ACK on exit
    dd 0                        ; 15 GT thread status on exit
    dd 0                        ; 16 registers read
    dd 0                        ; 17 MT ack value at a timeout, if any
    dd 0                        ; 18 reserved
    dd 0                        ; 19 reserved
    dd 0                        ; 20 reserved
    dd 0                        ; 21 reserved
    dd 0                        ; 22 reserved
    dd 0                        ; 23 reserved
IVGtCopy     dd IV_GT_COUNT dup (0)
IVGtFirst    dd IV_GT_COUNT dup (0)
IVGtSecond   dd IV_GT_COUNT dup (0)
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc IV_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     IVResult[24], eax
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
EndProc IV_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc IV_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short IV_Offset_Refused
    mov     esi, OFFSET32 IVWindows
    mov     ecx, IV_WINDOW_COUNT
IV_Offset_Next:
    cmp     edx, [esi]
    jb      short IV_Offset_Skip
    cmp     edx, [esi+4]
    jb      short IV_Offset_Accepted
IV_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short IV_Offset_Next
IV_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
IV_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc IV_Offset_Allowed

; AX = device id. Returns CF clear if it is in IVDevices.
BeginProc IV_Device_Known
    push    ecx
    push    esi
    mov     esi, OFFSET32 IVDevices
    mov     ecx, IV_DEVICE_COUNT
IV_Device_Next:
    cmp     ax, [esi]
    je      short IV_Device_Found
    add     esi, 2
    dec     ecx
    jnz     short IV_Device_Next
    pop     esi
    pop     ecx
    stc
    ret
IV_Device_Found:
    pop     esi
    pop     ecx
    clc
    ret
EndProc IV_Device_Known

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc IV_Accept_Offsets
    pushad
    mov     IVResult[16], 0
    sub     ecx, IV_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+IV_IN_COUNT]
    cmp     eax, IV_OFFSET_MAX
    ja      short IV_Accept_Refused
    cmp     eax, ecx
    ja      short IV_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+IV_IN_OFFSETS]
    mov     edi, OFFSET32 IVOffsets
    jecxz   short IV_Accept_Done
IV_Accept_Next:
    mov     edx, [esi]
    call    IV_Offset_Allowed
    jc      short IV_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short IV_Accept_Next
IV_Accept_Done:
    mov     eax, [ebp+IV_IN_COUNT]
    mov     IVResult[16], eax
    popad
    ret
IV_Accept_Refused_At:
    mov     IVResult[28], edx
IV_Accept_Refused:
    or      IVResult[4], IV_ST_OFFSETS_REFUSED
    popad
    ret
EndProc IV_Accept_Offsets

; EDI = destination. Reads every accepted offset through the MMIO mapping.
BeginProc IV_Read_Snapshot
    pushad
    mov     ecx, IVResult[16]
    mov     ebx, OFFSET32 IVOffsets
    mov     esi, IVMmio
    jecxz   short IV_Read_Snapshot_Done
IV_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short IV_Read_Snapshot_Next
IV_Read_Snapshot_Done:
    popad
    ret
EndProc IV_Read_Snapshot

; Copy the first IV_GTT_COPY entries (fewer if the table is smaller) and
; count valid ones (bit 0) in the whole table, IVResult[40] entries.
BeginProc IV_Read_Gtt
    pushad
    mov     esi, IVGttLinear
    mov     edi, OFFSET32 IVGtt
    mov     ecx, IVResult[40]
    cmp     ecx, IV_GTT_COPY
    jbe     short IV_Read_Gtt_Copy
    mov     ecx, IV_GTT_COPY
IV_Read_Gtt_Copy:
    cld
    rep     movsd

    mov     esi, IVGttLinear
    mov     ecx, IVResult[40]
    xor     edx, edx
IV_Read_Gtt_Next:
    test    dword ptr [esi], 1
    jz      short IV_Read_Gtt_Skip
    inc     edx
IV_Read_Gtt_Skip:
    add     esi, 4
    dec     ecx
    jnz     short IV_Read_Gtt_Next
    mov     IVResult[20], edx
    or      IVResult[4], IV_ST_GTT_READ
    popad
    ret
EndProc IV_Read_Gtt

BeginProc IV_Capture
    pushad

    ; The base must belong to an Ivy Bridge IGD at 00:02.0 and carry the
    ; BAR's own 4 MB alignment; anything else is refused before it is
    ; mapped.
    mov     eax, IV_IGD_ADDRESS
    call    IV_Pci_Read
    mov     IVResult[8], eax
    cmp     ax, IV_VENDOR_INTEL
    jne     IV_Capture_Bad_Base
    or      IVResult[4], IV_ST_IGD_INTEL
    shr     eax, 16
    call    IV_Device_Known
    jnc     short IV_Capture_Check_Base
    or      IVResult[4], IV_ST_NOT_IVB
    jmp     IV_Capture_Done

IV_Capture_Check_Base:
    mov     eax, IVBase
    test    eax, eax
    jz      IV_Capture_Bad_Base
    test    eax, IV_BASE_ALIGN
    jnz     IV_Capture_Bad_Base
    mov     IVResult[12], eax

    VMMcall _MapPhysToLinear,<eax,IV_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      IV_Capture_Done
    mov     IVMmio, eax
    or      IVResult[4], IV_ST_MMIO_MAPPED

    mov     edi, OFFSET32 IVFirst
    call    IV_Read_Snapshot
    mov     edi, OFFSET32 IVSecond
    call    IV_Read_Snapshot
    or      IVResult[4], IV_ST_SNAPSHOT_TAKEN

    ; Size the GTT from GGC GGMS: 1 or 2 MB. None and the reserved
    ; encoding are refused rather than guessed.
    mov     eax, IV_GGC_ADDRESS
    call    IV_Pci_Read
    mov     IVResult[32], eax
    shr     eax, IV_GGMS_SHIFT
    and     eax, IV_GGMS_MASK
    cmp     eax, 1
    je      short IV_Capture_Gtt_Size
    cmp     eax, 2
    je      short IV_Capture_Gtt_Size
    or      IVResult[4], IV_ST_GGMS_UNUSABLE
    jmp     short IV_Capture_Done

IV_Capture_Gtt_Size:
    shl     eax, 20
    mov     ecx, eax
    mov     IVResult[36], ecx
    shr     eax, 2
    mov     IVResult[40], eax
    mov     eax, IVBase
    add     eax, IV_GTT_OFFSET
    VMMcall _MapPhysToLinear,<eax,ecx,0>
    cmp     eax, 0ffffffffh
    je      short IV_Capture_Done
    mov     IVGttLinear, eax
    or      IVResult[4], IV_ST_GTT_MAPPED
    call    IV_Read_Gtt
    jmp     short IV_Capture_Done

IV_Capture_Bad_Base:
    or      IVResult[4], IV_ST_BASE_INVALID
IV_Capture_Done:
    popad
    ret
EndProc IV_Capture

; ESI = MMIO mapping, EDX = register offset, EBX = mask, EAX = the value
; (register AND mask) is waited for. Returns ECX = reads taken and CF set
; if IV_POLL_LIMIT reads passed without it; EAX = the last value read.
BeginProc IV_Poll
    push    edi
    mov     edi, eax
    xor     ecx, ecx
IV_Poll_Next:
    inc     ecx
    mov     eax, [esi+edx]
    push    eax
    and     eax, ebx
    cmp     eax, edi
    pop     eax
    je      short IV_Poll_Done
    cmp     ecx, IV_POLL_LIMIT
    jb      short IV_Poll_Next
    pop     edi
    stc
    ret
IV_Poll_Done:
    pop     edi
    clc
    ret
EndProc IV_Poll

; ESI = MMIO mapping, EDI = destination. Reads every IVGtOffsets register.
BeginProc IV_Gt_Read
    pushad
    mov     ebx, OFFSET32 IVGtOffsets
    mov     ecx, IV_GT_COUNT
IV_Gt_Read_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short IV_Gt_Read_Next
    popad
    ret
EndProc IV_Gt_Read

; The forcewake handshake and GT reads described in the header. The IGD
; and base checks are the display capture's.
BeginProc IV_Gt_Capture
    pushad

    mov     eax, IV_IGD_ADDRESS
    call    IV_Pci_Read
    mov     IVGtResult[8], eax
    cmp     ax, IV_VENDOR_INTEL
    jne     IV_Gt_Bad_Base
    or      IVGtResult[4], IV_GT_ST_IGD_OK
    shr     eax, 16
    call    IV_Device_Known
    jnc     short IV_Gt_Check_Base
    or      IVGtResult[4], IV_GT_ST_NOT_IVB
    jmp     IV_Gt_Done

IV_Gt_Check_Base:
    mov     eax, IVBase
    test    eax, eax
    jz      IV_Gt_Bad_Base
    test    eax, IV_BASE_ALIGN
    jnz     IV_Gt_Bad_Base
    mov     IVGtResult[12], eax
    VMMcall _MapPhysToLinear,<eax,IV_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      IV_Gt_Done
    mov     esi, eax
    or      IVGtResult[4], IV_GT_ST_MAPPED

    pushfd
    cli

    ; State on entry, all three registers i915 reads raw.
    mov     eax, [esi+IV_FW_MT_ACK]
    mov     IVGtResult[16], eax
    mov     eax, [esi+IV_FW_LEGACY_ACK]
    mov     IVGtResult[20], eax
    mov     eax, [esi+IV_THREAD_STATUS]
    mov     IVGtResult[24], eax

    ; i915's IVB init: clear the legacy request, post through ECOBUS, then
    ; clear every MT thread bit.
    mov     dword ptr [esi+IV_FW_LEGACY], 0
    mov     eax, [esi+IV_ECOBUS]
    mov     IVGtResult[28], eax
    mov     dword ptr [esi+IV_FW_MT], IV_FW_CLEAR_ALL

    ; The acknowledge must clear before bit 0 is requested. If it does
    ; not, nothing more is written: the request register is already
    ; clear, which is the state i915 leaves at reset.
    mov     edx, IV_FW_MT_ACK
    mov     ebx, IV_FW_KERNEL
    xor     eax, eax
    call    IV_Poll
    mov     IVGtResult[32], ecx
    jnc     short IV_Gt_Request
    mov     IVGtResult[68], eax
    or      IVGtResult[4], IV_GT_ST_ACK_STUCK
    jmp     IV_Gt_Exit_State

IV_Gt_Request:
    mov     dword ptr [esi+IV_FW_MT], IV_FW_SET_KERNEL
    mov     edx, IV_FW_MT_ACK
    mov     ebx, IV_FW_KERNEL
    mov     eax, IV_FW_KERNEL
    call    IV_Poll
    mov     IVGtResult[36], ecx
    jnc     short IV_Gt_Thread
    mov     IVGtResult[68], eax
    or      IVGtResult[4], IV_GT_ST_SET_TIMEOUT
    jmp     short IV_Gt_Put

IV_Gt_Thread:
    mov     edx, IV_THREAD_STATUS
    mov     ebx, IV_THREAD_MASK
    xor     eax, eax
    call    IV_Poll
    mov     IVGtResult[40], ecx
    jnc     short IV_Gt_Reads
    or      IVGtResult[4], IV_GT_ST_THREAD_TIMEOUT
    jmp     short IV_Gt_Put

IV_Gt_Reads:
    mov     edi, OFFSET32 IVGtFirst
    call    IV_Gt_Read
    mov     edi, OFFSET32 IVGtSecond
    call    IV_Gt_Read
    mov     IVGtResult[64], IV_GT_COUNT
    or      IVGtResult[4], IV_GT_ST_READ_DONE

IV_Gt_Put:
    mov     dword ptr [esi+IV_FW_MT], IV_FW_CLEAR_KERNEL
    mov     edx, IV_FW_MT_ACK
    mov     ebx, IV_FW_KERNEL
    xor     eax, eax
    call    IV_Poll
    mov     IVGtResult[44], ecx
    jnc     short IV_Gt_Restore
    or      IVGtResult[4], IV_GT_ST_PUT_TIMEOUT

IV_Gt_Restore:
    ; Bit 0 was acknowledged on entry: request it again, so the GT is
    ; left as the BIOS left it.
    test    IVGtResult[16], IV_FW_KERNEL
    jz      short IV_Gt_Exit_State
    mov     dword ptr [esi+IV_FW_MT], IV_FW_SET_KERNEL
    mov     edx, IV_FW_MT_ACK
    mov     ebx, IV_FW_KERNEL
    mov     eax, IV_FW_KERNEL
    call    IV_Poll
    mov     IVGtResult[48], ecx
    jc      short IV_Gt_Restore_Timeout
    or      IVGtResult[4], IV_GT_ST_RESTORED
    jmp     short IV_Gt_Exit_State
IV_Gt_Restore_Timeout:
    or      IVGtResult[4], IV_GT_ST_RESTORE_TIMEOUT

IV_Gt_Exit_State:
    mov     eax, [esi+IV_FW_MT_ACK]
    mov     IVGtResult[52], eax
    mov     eax, [esi+IV_FW_LEGACY_ACK]
    mov     IVGtResult[56], eax
    mov     eax, [esi+IV_THREAD_STATUS]
    mov     IVGtResult[60], eax
    popfd
    jmp     short IV_Gt_Done

IV_Gt_Bad_Base:
    or      IVGtResult[4], IV_GT_ST_BAD_BASE
IV_Gt_Done:
    popad
    ret
EndProc IV_Gt_Capture

BeginProc IV_Gt_Reset_Result
    pushad
    mov     edi, OFFSET32 IVGtResult + 4
    mov     ecx, IV_GT_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     esi, OFFSET32 IVGtOffsets
    mov     edi, OFFSET32 IVGtCopy
    mov     ecx, IV_GT_COUNT
    rep     movsd
    popad
    ret
EndProc IV_Gt_Reset_Result

; EBX = DIOCParams. DIOC 2: input is the base dword alone.
BeginProc IV_Gt_Dioc
    pushad
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short IV_Gt_Dioc_Fail
    cmp     [ebx.cbInBuffer], 4
    jb      short IV_Gt_Dioc_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short IV_Gt_Dioc_Fail
    cmp     [ebx.cbOutBuffer], IV_GT_RESULT_DWORDS * 4
    jb      short IV_Gt_Dioc_Fail

    call    IV_Gt_Reset_Result
    mov     eax, [ebp]
    mov     IVBase, eax
    call    IV_Gt_Capture

    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 IVGtResult
    mov     ecx, IV_GT_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short IV_Gt_Dioc_Ok
    mov     dword ptr [eax], IV_GT_RESULT_DWORDS * 4
IV_Gt_Dioc_Ok:
    popad
    clc
    ret
IV_Gt_Dioc_Fail:
    popad
    stc
    ret
EndProc IV_Gt_Dioc

BeginProc IV_Reset_Result
    pushad
    mov     edi, OFFSET32 IVResult + 4
    mov     ecx, IV_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     IVMmio, eax
    mov     IVGttLinear, eax
    popad
    ret
EndProc IV_Reset_Result

BeginProc IV_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      IV_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      IV_Dioc_Ok
    cmp     ecx, IV_DIOC_GT
    je      short IV_Dioc_Route_Gt
    cmp     ecx, IV_DIOC_CAPTURE
    jne     IV_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short IV_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, IV_IN_MIN_BYTES
    jb      short IV_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short IV_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], IV_RESULT_DWORDS * 4
    jb      short IV_Dioc_Copy_Fail

    call    IV_Reset_Result
    mov     eax, [ebp+IV_IN_BASE]
    mov     IVBase, eax
    call    IV_Accept_Offsets
    test    IVResult[4], IV_ST_OFFSETS_REFUSED
    jnz     short IV_Dioc_Copy_Out
    call    IV_Capture

IV_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 IVResult
    mov     ecx, IV_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short IV_Dioc_Copy_Done
    mov     dword ptr [eax], IV_RESULT_DWORDS * 4
IV_Dioc_Copy_Done:
    popad
IV_Dioc_Ok:
    xor     eax, eax
    ret

IV_Dioc_Route_Gt:
    push    ebx
    mov     ebx, esi
    call    IV_Gt_Dioc
    pop     ebx
    jc      short IV_Dioc_Fail
    xor     eax, eax
    ret

IV_Dioc_Copy_Fail:
    popad
IV_Dioc_Fail:
    mov     eax, 1
    ret
EndProc IV_W32_DeviceIoControl

BeginProc IV_Dynamic_Init
    clc
    ret
EndProc IV_Dynamic_Init

BeginProc IV_Dynamic_Exit
    clc
    ret
EndProc IV_Dynamic_Exit

Begin_Control_Dispatch IV
    Control_Dispatch Sys_Dynamic_Device_Init, IV_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, IV_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, IV_W32_DeviceIoControl
End_Control_Dispatch IV

VxD_LOCKED_CODE_ENDS

end
