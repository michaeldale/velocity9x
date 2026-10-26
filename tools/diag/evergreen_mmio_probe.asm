; AMD Evergreen (Radeon HD 5450, Cedar) read-only register probe VxD.
;
; Phase 2 of docs\plans\ati-evergreen-cedar.md. Maps the 128 KiB register
; BAR (BAR2, a 64-bit memory BAR) and reads a caller's list of offsets, each
; twice. The Rage 128 probe (r128_mmio_probe.asm) is the model.
;
; Writes: none, to the device or to memory. 0CF8h is saved and restored
; around each configuration read. Mapping a physical range with
; _MapPhysToLinear changes no device state.
;
; The caller names the device by its configuration address; the VxD reads
; the id and BAR2 itself and refuses anything that is not 1002:68F9 with a
; 128 KiB-aligned memory BAR2 below 4 GiB. A 64-bit BAR (type bits 2:1 =
; 10b) must read 0 in its high half at 1Ch, or nothing is mapped: half of a
; BAR above 4 GiB would map some other physical range.
;
; Evergreen keeps no copy of configuration space in the register file that
; any source to hand names, so the identity check after mapping is weaker
; than the Rage 128's: CONFIG_MEMSIZE (5428h, MiB on Evergreen, Linux
; evergreen.c evergreen_mc_init) must be non-zero and at most 4096. A
; window that decodes nothing reads FFFFFFFFh and fails it.
;
; Offsets are accepted only inside EGWindows below, and only for the four
; display controllers Cedar has (Linux radeon_asic.c: num_crtc = 4 for
; CHIP_CEDAR). Left out on purpose, because a read has a side effect or
; reads through an index:
;   0000h-001Bh  MM_INDEX / MM_DATA, the PIF PHY index/data pairs, and
;                MM_INDEX_HI (evergreen_reg.h);
;   08F8h-08FFh  CG_IND_ADDR / CG_IND_DATA;
;   5480h        HDP_MEM_COHERENCY_FLUSH_CNTL, a flush trigger;
;   69E8h-69F3h  DC_LUT_SEQ_COLOR, DC_LUT_PWL_DATA, DC_LUT_30_COLOR: the
;                palette data ports, whose access advances the LUT index.
; Offsets are Linux radeon's evergreend.h, evergreen_reg.h and r600d.h
; (build\external\evergreen\linux-radeon\), not checked against any AMD
; register guide: none for these blocks was found.

.386p

.xlist
include VMM.INC
include VWIN32.INC
.list

Declare_Virtual_Device EGMM, 1, 0, EG_Control, \
                       Undefined_Device_ID, Undefined_Init_Order, , ,

EG_MAGIC                equ 4d4d4745h ; "EGMM"
EG_DIOC_CAPTURE         equ 1
EG_VENDOR_ATI           equ 1002h
EG_DEVICE_CEDAR         equ 68f9h
EG_CFG_BAR2             equ 18h
EG_CFG_BAR2_HIGH        equ 1ch
EG_MMIO_BYTES           equ 00020000h
EG_BASE_ALIGN           equ 0001ffffh
EG_BAR_IO               equ 00000001h
EG_BAR_TYPE_MASK        equ 00000006h
EG_BAR_TYPE_64          equ 00000004h
EG_CONFIG_MEMSIZE       equ 5428h
EG_MEMSIZE_MAX_MIB      equ 4096
EG_OFFSET_MAX           equ 256

; Status bits returned to the publisher.
EG_ST_ATI               equ 00000001h
EG_ST_BASE_INVALID      equ 00000002h
EG_ST_MMIO_MAPPED       equ 00000004h
EG_ST_IDENTITY_MATCH    equ 00000008h
EG_ST_OFFSETS_REFUSED   equ 00000010h
EG_ST_SNAPSHOT_TAKEN    equ 00000020h
EG_ST_BAR_64            equ 00000040h
EG_ST_NOT_CEDAR         equ 00000080h
EG_ST_ADDRESS_INVALID   equ 00000100h
EG_ST_BAR_ABOVE_4G      equ 00000200h

; Input from V9XEGMM.EXE.
EG_IN_CONFIG            equ 0
EG_IN_COUNT             equ 4
EG_IN_OFFSETS           equ 8
EG_IN_MIN_BYTES         equ 8

; Output layout, in dwords. Must match struct v9x_egmm_result in
; evergreen_mmio_probe_win32.c.
EG_HEADER_DWORDS        equ 16
EG_RESULT_DWORDS        equ (EG_HEADER_DWORDS + EG_OFFSET_MAX * 3)

VxD_LOCKED_DATA_SEG

; Accepted windows: start, limit (exclusive). The display blocks repeat at
; the CRTC offsets 0, 0C00h, 9800h and A400h (evergreen_reg.h).
EGWindows label dword
    dd 0300h, 0340h     ; VGA_RENDER_CONTROL, VGA memory base, D1/D2 VGA control
    dd 0e50h, 0e54h     ; SRBM_STATUS
    dd 0e60h, 0e64h     ; SRBM_SOFT_RESET
    dd 0ec4h, 0ec8h     ; SRBM_STATUS2
    dd 1400h, 1404h     ; VM_L2_CNTL
    dd 1410h, 1414h     ; VM_CONTEXT0_CNTL
    dd 2004h, 2008h     ; MC_SHARED_CHMAP
    dd 2024h, 2040h     ; MC_VM_FB_LOCATION, AGP_TOP/BOT/BASE, SYSTEM_APERTURE_*
    dd 2760h, 2764h     ; MC_ARB_RAMCFG
    dd 2c04h, 2c10h     ; HDP_NONSURFACE_BASE/INFO/SIZE
    dd 2f48h, 2f4ch     ; HDP_ADDR_CONFIG
    dd 3f00h, 3f04h     ; RLC_CNTL
    dd 5420h, 542ch     ; BUS_CNTL, CONFIG_CNTL, CONFIG_MEMSIZE
    dd 5490h, 5494h     ; BIF_FB_EN
    dd 8010h, 8018h     ; GRBM_STATUS, GRBM_STATUS_SE0
    dd 8020h, 8024h     ; GRBM_SOFT_RESET
    dd 8680h, 8684h     ; CP_STAT
    dd 86d8h, 86dch     ; CP_ME_CNTL
    dd 98f8h, 98fch     ; GB_ADDR_CONFIG
    dd 0c100h, 0c118h   ; CP_RB_BASE, CP_RB_CNTL .. CP_RB_WPTR
    dd 0d000h, 0d038h   ; DMA_RB_CNTL .. DMA_STATUS_REG
    dd 0d044h, 0d04ch   ; DMA_SEM_*_TIMER_CNTL
    dd 0d0b8h, 0d0c0h   ; DMA_TILING_CONFIG, DMA_MODE
    ; CRTC0
    dd 6800h, 684ch     ; GRPH_ENABLE .. GRPH_FLIP_CONTROL
    dd 6998h, 69bch     ; CUR_CONTROL .. CUR_UPDATE
    dd 69e0h, 69e8h     ; DC_LUT_RW_MODE, DC_LUT_RW_INDEX
    dd 69f4h, 6a1ch     ; DC_LUT_VGA_ACCESS_ENABLE .. DC_LUT_WHITE_OFFSET_RED
    dd 6d70h, 6d78h     ; VIEWPORT_START, VIEWPORT_SIZE
    dd 6e34h, 6e38h     ; CRTC_V_BLANK_START_END
    dd 6e70h, 6e78h     ; CRTC_CONTROL, CRTC_BLANK_CONTROL
    dd 6e8ch, 6e94h     ; CRTC_STATUS, CRTC_STATUS_POSITION
    dd 6ea0h, 6ea4h     ; CRTC_STATUS_HV_COUNT
    dd 6ed4h, 6ed8h     ; CRTC_UPDATE_LOCK
    dd 6ef4h, 6efch     ; MASTER_UPDATE_LOCK, MASTER_UPDATE_MODE
    ; CRTC1 (+0C00h)
    dd 7400h, 744ch
    dd 7598h, 75bch
    dd 75e0h, 75e8h
    dd 75f4h, 761ch
    dd 7970h, 7978h
    dd 7a34h, 7a38h
    dd 7a70h, 7a78h
    dd 7a8ch, 7a94h
    dd 7aa0h, 7aa4h
    dd 7ad4h, 7ad8h
    dd 7af4h, 7afch
    ; CRTC2 (+9800h)
    dd 10000h, 1004ch
    dd 10198h, 101bch
    dd 101e0h, 101e8h
    dd 101f4h, 1021ch
    dd 10570h, 10578h
    dd 10634h, 10638h
    dd 10670h, 10678h
    dd 1068ch, 10694h
    dd 106a0h, 106a4h
    dd 106d4h, 106d8h
    dd 106f4h, 106fch
    ; CRTC3 (+A400h)
    dd 10c00h, 10c4ch
    dd 10d98h, 10dbch
    dd 10de0h, 10de8h
    dd 10df4h, 10e1ch
    dd 11170h, 11178h
    dd 11234h, 11238h
    dd 11270h, 11278h
    dd 1128ch, 11294h
    dd 112a0h, 112a4h
    dd 112d4h, 112d8h
    dd 112f4h, 112fch
EG_WINDOW_COUNT         equ 67

EGResult label dword
    dd EG_MAGIC                 ; 00 magic
    dd 0                        ; 01 status
    dd 0                        ; 02 id dword at configuration 00h
    dd 0                        ; 03 BAR2 as read
    dd 0                        ; 04 offsets accepted
    dd 0                        ; 05 CONFIG_MEMSIZE through the BAR
    dd 0                        ; 06 0CF8h found and restored
    dd 0                        ; 07 first offset refused, if any
    dd 0                        ; 08 configuration address used
    dd 0                        ; 09 BAR2 high dword (1Ch), 64-bit BARs only
    dd 0                        ; 10 reserved
    dd 0                        ; 11 reserved
    dd 0                        ; 12 reserved
    dd 0                        ; 13 reserved
    dd 0                        ; 14 reserved
    dd 0                        ; 15 reserved
EGOffsets    dd EG_OFFSET_MAX dup (0)
EGFirst      dd EG_OFFSET_MAX dup (0)
EGSecond     dd EG_OFFSET_MAX dup (0)
EGConfig     dd 0
EGMmio       dd 0
VxD_LOCKED_DATA_ENDS

VxD_LOCKED_CODE_SEG

; EAX = PCI configuration address. Returns EAX = configuration dword.
; 0CF8h saved and restored with interrupts off.
BeginProc EG_Pci_Read
    push    ebx
    push    edx
    pushfd
    cli
    mov     ebx, eax
    mov     dx, 0cf8h
    in      eax, dx
    mov     EGResult[24], eax
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
EndProc EG_Pci_Read

; EDX = offset. Returns CF clear if it lies in an accepted window.
BeginProc EG_Offset_Allowed
    push    ecx
    push    esi
    test    edx, 3
    jnz     short EG_Offset_Refused
    mov     esi, OFFSET32 EGWindows
    mov     ecx, EG_WINDOW_COUNT
EG_Offset_Next:
    cmp     edx, [esi]
    jb      short EG_Offset_Skip
    cmp     edx, [esi+4]
    jb      short EG_Offset_Accepted
EG_Offset_Skip:
    add     esi, 8
    dec     ecx
    jnz     short EG_Offset_Next
EG_Offset_Refused:
    pop     esi
    pop     ecx
    stc
    ret
EG_Offset_Accepted:
    pop     esi
    pop     ecx
    clc
    ret
EndProc EG_Offset_Allowed

; EBP = DIOC input buffer, ECX = its byte count. Copies the offset list,
; refusing the whole list if any entry is unaligned or outside the windows.
BeginProc EG_Accept_Offsets
    pushad
    mov     EGResult[16], 0
    sub     ecx, EG_IN_OFFSETS
    shr     ecx, 2
    mov     eax, [ebp+EG_IN_COUNT]
    cmp     eax, EG_OFFSET_MAX
    ja      short EG_Accept_Refused
    cmp     eax, ecx
    ja      short EG_Accept_Refused
    mov     ecx, eax
    lea     esi, [ebp+EG_IN_OFFSETS]
    mov     edi, OFFSET32 EGOffsets
    jecxz   short EG_Accept_Done
EG_Accept_Next:
    mov     edx, [esi]
    call    EG_Offset_Allowed
    jc      short EG_Accept_Refused_At
    mov     [edi], edx
    add     esi, 4
    add     edi, 4
    dec     ecx
    jnz     short EG_Accept_Next
EG_Accept_Done:
    mov     eax, [ebp+EG_IN_COUNT]
    mov     EGResult[16], eax
    popad
    ret
EG_Accept_Refused_At:
    mov     EGResult[28], edx
EG_Accept_Refused:
    or      EGResult[4], EG_ST_OFFSETS_REFUSED
    popad
    ret
EndProc EG_Accept_Offsets

; EDI = destination. Reads every accepted offset through the mapping.
BeginProc EG_Read_Snapshot
    pushad
    mov     ecx, EGResult[16]
    mov     ebx, OFFSET32 EGOffsets
    mov     esi, EGMmio
    jecxz   short EG_Read_Snapshot_Done
EG_Read_Snapshot_Next:
    mov     eax, [ebx]
    mov     eax, [esi+eax]
    mov     [edi], eax
    add     ebx, 4
    add     edi, 4
    dec     ecx
    jnz     short EG_Read_Snapshot_Next
EG_Read_Snapshot_Done:
    popad
    ret
EndProc EG_Read_Snapshot

BeginProc EG_Capture
    pushad

    ; A configuration address: enable bit set, register field zero, and
    ; nothing in the reserved bits 30:24.
    mov     eax, EGConfig
    mov     EGResult[32], eax
    test    eax, 80000000h
    jz      EG_Capture_Bad_Address
    test    eax, 7f0000ffh
    jnz     EG_Capture_Bad_Address

    call    EG_Pci_Read
    mov     EGResult[8], eax
    cmp     ax, EG_VENDOR_ATI
    jne     EG_Capture_Bad_Base
    or      EGResult[4], EG_ST_ATI
    shr     eax, 16
    cmp     ax, EG_DEVICE_CEDAR
    je      short EG_Capture_Check_Base
    or      EGResult[4], EG_ST_NOT_CEDAR
    jmp     EG_Capture_Done

EG_Capture_Check_Base:
    mov     eax, EGConfig
    or      eax, EG_CFG_BAR2
    call    EG_Pci_Read
    mov     EGResult[12], eax
    test    eax, EG_BAR_IO
    jnz     EG_Capture_Bad_Base
    mov     edx, eax
    and     edx, EG_BAR_TYPE_MASK
    cmp     edx, EG_BAR_TYPE_64
    jne     short EG_Capture_Low_Base

    ; A 64-bit BAR: its high half must be zero.
    or      EGResult[4], EG_ST_BAR_64
    push    eax
    mov     eax, EGConfig
    or      eax, EG_CFG_BAR2_HIGH
    call    EG_Pci_Read
    mov     EGResult[36], eax
    test    eax, eax
    pop     eax
    jz      short EG_Capture_Low_Base
    or      EGResult[4], EG_ST_BAR_ABOVE_4G
    jmp     EG_Capture_Bad_Base

EG_Capture_Low_Base:
    and     eax, NOT 0fh
    jz      EG_Capture_Bad_Base
    test    eax, EG_BASE_ALIGN
    jnz     EG_Capture_Bad_Base

    VMMcall _MapPhysToLinear,<eax,EG_MMIO_BYTES,0>
    cmp     eax, 0ffffffffh
    je      EG_Capture_Done
    mov     EGMmio, eax
    or      EGResult[4], EG_ST_MMIO_MAPPED

    mov     edx, [eax+EG_CONFIG_MEMSIZE]
    mov     EGResult[20], edx
    test    edx, edx
    jz      short EG_Capture_Done
    cmp     edx, EG_MEMSIZE_MAX_MIB
    ja      short EG_Capture_Done
    or      EGResult[4], EG_ST_IDENTITY_MATCH

    mov     edi, OFFSET32 EGFirst
    call    EG_Read_Snapshot
    mov     edi, OFFSET32 EGSecond
    call    EG_Read_Snapshot
    or      EGResult[4], EG_ST_SNAPSHOT_TAKEN
    jmp     short EG_Capture_Done

EG_Capture_Bad_Address:
    or      EGResult[4], EG_ST_ADDRESS_INVALID
    jmp     short EG_Capture_Done
EG_Capture_Bad_Base:
    or      EGResult[4], EG_ST_BASE_INVALID
EG_Capture_Done:
    popad
    ret
EndProc EG_Capture

BeginProc EG_Reset_Result
    pushad
    mov     edi, OFFSET32 EGResult + 4
    mov     ecx, EG_RESULT_DWORDS - 1
    xor     eax, eax
    cld
    rep     stosd
    mov     EGMmio, eax
    popad
    ret
EndProc EG_Reset_Result

BeginProc EG_W32_DeviceIoControl
    cmp     ecx, DIOC_OPEN
    je      EG_Dioc_Ok
    cmp     ecx, DIOC_CLOSEHANDLE
    je      EG_Dioc_Ok
    cmp     ecx, EG_DIOC_CAPTURE
    jne     EG_Dioc_Fail

    pushad
    mov     ebx, esi
    mov     ebp, [ebx.lpvInBuffer]
    test    ebp, ebp
    jz      short EG_Dioc_Copy_Fail
    mov     ecx, [ebx.cbInBuffer]
    cmp     ecx, EG_IN_MIN_BYTES
    jb      short EG_Dioc_Copy_Fail
    mov     edi, [ebx.lpvOutBuffer]
    test    edi, edi
    jz      short EG_Dioc_Copy_Fail
    cmp     [ebx.cbOutBuffer], EG_RESULT_DWORDS * 4
    jb      short EG_Dioc_Copy_Fail

    call    EG_Reset_Result
    mov     eax, [ebp+EG_IN_CONFIG]
    mov     EGConfig, eax
    call    EG_Accept_Offsets
    test    EGResult[4], EG_ST_OFFSETS_REFUSED
    jnz     short EG_Dioc_Copy_Out
    call    EG_Capture

EG_Dioc_Copy_Out:
    mov     edi, [ebx.lpvOutBuffer]
    mov     esi, OFFSET32 EGResult
    mov     ecx, EG_RESULT_DWORDS
    cld
    rep     movsd
    mov     eax, [ebx.lpcbBytesReturned]
    test    eax, eax
    jz      short EG_Dioc_Copy_Done
    mov     dword ptr [eax], EG_RESULT_DWORDS * 4
EG_Dioc_Copy_Done:
    popad
EG_Dioc_Ok:
    xor     eax, eax
    ret

EG_Dioc_Copy_Fail:
    popad
EG_Dioc_Fail:
    mov     eax, 1
    ret
EndProc EG_W32_DeviceIoControl

BeginProc EG_Dynamic_Init
    clc
    ret
EndProc EG_Dynamic_Init

BeginProc EG_Dynamic_Exit
    clc
    ret
EndProc EG_Dynamic_Exit

Begin_Control_Dispatch EG
    Control_Dispatch Sys_Dynamic_Device_Init, EG_Dynamic_Init
    Control_Dispatch Sys_Dynamic_Device_Exit, EG_Dynamic_Exit
    Control_Dispatch W32_DeviceIoControl, EG_W32_DeviceIoControl
End_Control_Dispatch EG

VxD_LOCKED_CODE_ENDS

end
